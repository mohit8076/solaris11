/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * FMD Dynamic Reconfiguration (DR) Event Handling
 *
 * Fault manager scheme plug-ins must track characteristics of individual
 * pieces of hardware.  As these components can be added or removed by a DR
 * operation, we need to provide a means by which plug-ins can determine when
 * they need to re-examine the current configuration.  We provide a simple
 * mechanism whereby this task can be implemented using lazy evaluation: a
 * simple 64-bit generation counter is maintained and incremented on *any* DR.
 * Schemes can store the generation number in scheme-specific data structures,
 * and then revalidate their contents if the current generation number has
 * changed since the resource information was cached.  This method saves time,
 * avoids the complexity of direct participation in DR, avoids the need for
 * resource-specific processing of DR events, and is relatively easy to port
 * to other systems that support dynamic reconfiguration.
 *
 * The dr generation is only incremented in response to hardware changes.  Since
 * ASRUs can be in any scheme, including the device scheme, we must also be
 * aware of software configuration changes which may affect the resource cache.
 * In addition, we take a topo snapshot of the topology whenever a non-filtered
 * reconfiguration event occurs and notify any modules of the change.
 */

#include <sys/types.h>
#include <sys/sunddi.h>
#include <sys/sysevent/dr.h>
#include <sys/sysevent/eventdefs.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libsysevent.h>

#undef MUTEX_HELD
#undef RW_READ_HELD
#undef RW_WRITE_HELD

#include <fmd_asru.h>
#include <fmd_ctl.h>
#include <fmd_error.h>
#include <fmd_event.h>
#include <fmd_fmri.h>
#include <fmd_module.h>
#include <fmd_subr.h>
#include <fmd_topo.h>
#include <fmd.h>

/*
 * Filter out non-DR events based on class.
 *
 * NOTE: Older code considered EC_DEV_ADD and EC_DEV_REMOVE events with
 * a subclass of ESC_DISK as topology changes.  These EC_DEV_* events
 * are generated by devfsadmd when public /dev name creation is
 * complete. The long term goal is to remove dependencies on public
 * names from topo snapshot generation code ... but we are not there
 * yet. The current code filters out EC_DEV_* events, but compensates
 * for this by adding DI_MAKE_LINK to disk_common.c code. While the
 * 'ses' topo enumerator still has dependencies on public names,
 * ensuring these dependencies were (and still are) met is coincidental
 * on finding disks: the old code was specific to ESC_DISK, and the new
 * DI_MAKE_LINK approach is dependent on disk_common.c finding a disk.
 */
static int
filtered_by_class(sysevent_t *ev)
{
	const char	*class = sysevent_get_class_name(ev);
	const char	*subclass = sysevent_get_subclass_name(ev);

	fmd_dprintf(FMD_DBG_TOPO, "%s: class=%s\n", __func__, class);

	if (strcmp(class, EC_DEVFS) == 0) {
		/*
		 * For EC_DEVFS, filter out event subclasses that don't
		 * imply a topology change.
		 *
		 * NOTE: All the *_ADD|*_REMOVE|*_CREATE subclasses are
		 * considered topology changes, so are not filtered.
		 */
		if ((strcmp(subclass, ESC_DEVFS_INSTANCE_MOD) == 0) ||
		    (strcmp(subclass, ESC_DEVFS_START) == 0))
			return (1);
	} else if (strcmp(class, EC_DR) == 0) {
		/*
		 * For EC_DR, only *_STATE_CHANGE subclasses imply a topology
		 * change, other EC_DR subclasses do not.
		 */
		if ((strcmp(subclass, ESC_DR_AP_STATE_CHANGE) != 0) &&
		    (strcmp(subclass, ESC_DR_TARGET_STATE_CHANGE) != 0))
			return (1);
	} else if (strcmp(class, EC_PLATFORM) == 0) {
		/*
		 * For EC_PLATFORM, since we rely on the SP to enumerate
		 * fans, power-supplies and sensors/leds, filter everything
		 * but SP resets.
		 */
		if (strcmp(subclass, ESC_PLATFORM_SP_RESET) != 0)
			return (1);
	} else if (strcmp(class, EC_TOPO) == 0) {
		/*
		 * Any topology change sysevent that we get now should
		 * cause a topology snapshot.
		 */
		return (0);
	} else if (strcmp(class, EC_CRO) == 0) {
		/*
		 * For EC_CRO, only ESC_CRO_TOPOREFRESH subclass implies a
		 * topology change. We get this for new enumeration
		 * capabilities and aliases.
		 */
		if (strcmp(subclass, ESC_CRO_TOPOREFRESH) != 0)
			return (1);
	} else {
		return (1);		/* filtered */
	}

	return (0);			/* not filtered */
}

/*
 * Filter out DR events based on topo_snapshot.vs.event time.
 *
 * Picking an accurate time here is difficult.  On one hand, we have
 * the timestamp of the underlying sysevent, indicating when the
 * reconfiguration event occurred. We use this timestamp to queue the
 * topo event in order to avoid the race condition where an error
 * happens after the DR event, but before we start the snapshot.
 * On the other hand, we are taking the topo snapshot asynchronously,
 * and hence any additional DR events that happen before we begin
 * taking the topo snapshot can safely be ignored as the resultant
 * topo will be the same.
 *
 * Along these lines, we keep track of the last time we started a
 * topo snapshot.  If the sysevent occurred before the last topo
 * snapshot, then don't bother dispatching another topo change event.
 * We've already indicated (to the best of our ability) the change in
 * topology.  This prevents endless topo snapshots in response to a
 * flurry of sysevents.
 *
 * NOTE: The topology may actually have changed, but taking a new
 * topology snapshot will not be fruitful. It is possible to lose
 * a topology snapshot if the DR events occur faster than we can
 * detect them and generate topology.
 *
 * NOTE: If 'fmstat -a' shows that dr.filter_cna_dev filtering is
 * catching everything that dr.filter_time would (and more), then the
 * dr_filter_time code can be removed (to make this assessment, the
 * dr_filter_cna_dev code must come before dr_filter_time).
 */
static int
filtered_by_time(sysevent_t *ev)
{
	fmd_topo_t	*ftp_cur;
	hrtime_t	time_ev;
	hrtime_t	time_topo;

	ftp_cur = fmd_topo_hold(__func__);
	sysevent_get_time(ev, &time_ev);
	time_topo = ftp_cur->ft_time_begin;
	fmd_topo_rele(ftp_cur, __func__);

	fmd_dprintf(FMD_DBG_TOPO, "%s: ev_time=0x%llx, ft_time=0x%llx\n",
	    __func__, time_ev, time_topo);

	if (time_ev <= time_topo)
		return (1);		/* filtered */
	return (0);			/* not filtered */
}

/*
 * Filter out DR events that occurred at-or-before the CNA_DEV
 * (Configuration Numeric Association) of the associated node in
 * the current topo snapshot.
 *
 * Taking a topo snapshot can take a considerable amount of time - CNA_DEV
 * filtering helps filter out events that occur while actively taking a
 * topo snapshot (where the current topo snapshot already reflects the
 * configuration change).
 *
 * We pull the EA_CNA_DEV from the event. Only events that relate to
 * the device tree have EA_CNA_DEV CNA. We then correlate event CNA_DEV
 * with the devinfo/topo configuration CNA_DEV: filtering events that
 * occur at-or-before the current topo snapshot.
 *
 * We also pull DEVFS_PATHNAME from the event and, if available, we get
 * the cna_dev_topo from that node in the current devinfo snapshot (if
 * pathname is not available we get cna_dev_topo from the root of the
 * devinfo tree).
 *
 * NOTE: At some point cna_debug can be deleted.
 */
static void
cna_debug(uint64_t x)
{
#ifdef	DEBUG
	fmd.d_stats->ds_dr_filter_debug.fmds_value.ui64 += x;
#endif	/* DEBUG */
}

static int
filtered_by_cna(sysevent_t *ev)
{
	nvlist_t	*attr_list = NULL;
	char		*pathname;
	uint64_t	cna_dev_ev;
	uint64_t	cna_dev_topo;
	struct topo_hdl	*thp_cur;
	di_node_t	thdi;
	char		*p;
	di_node_t	di_path;
	di_path_t	pi_path;
	const char	*subclass = sysevent_get_subclass_name(ev);

	if (sysevent_get_attr_list(ev, &attr_list) == 0) {
		if (nvlist_lookup_uint64(attr_list,
		    EA_CNA_DEV, &cna_dev_ev) != 0)
			cna_dev_ev = 0;
		if (nvlist_lookup_string(attr_list,
		    DEVFS_PATHNAME, &pathname) == 0)
			pathname = strdup(pathname);
		else
			pathname = NULL;
		nvlist_free(attr_list);
	} else {
		cna_dev_ev = 0;
		pathname = NULL;
	}

	if (cna_dev_ev) {
		cna_dev_topo = 0;
		cna_debug(0x0001000000000000LL);
		thp_cur = fmd_fmri_topo_hold(TOPO_VERSION);
		if (thp_cur) {
			cna_debug(0x1000000000LL);
			thdi = topo_hdl_devinfo(thp_cur);
			if (thdi) {
				if (pathname) {
					cna_debug(0x1000000LL);
					p = NULL;
pagain:					di_path = di_lookup_node(thdi,
					    pathname);
					pi_path = di_lookup_path(thdi,
					    pathname);

					/*
					 * For 'remove' events, if pathname
					 * does not resolve we go up by
					 * removing the last component of the
					 * path).
					 */
					if ((p == NULL) &&
					    (di_path == NULL) &&
					    (pi_path == NULL) &&
					    strstr(subclass, "remove")) {
						p = strrchr(pathname, '/');
						if (p && (p != pathname)) {
							*p = 0;
							cna_debug(0x10000LL);
							goto pagain;
						}
					}

					if (di_path) {
						cna_debug(0x100LL);
						cna_dev_topo =
						    di_node_cna_dev(di_path);
					} else if (pi_path) {
						cna_debug(0x1LL);
						cna_dev_topo =
						    di_path_cna_dev(pi_path);
					} else
						cna_dev_topo =
						    di_cna_dev(thdi);
				} else
					cna_dev_topo = di_cna_dev(thdi);
			}
			fmd_fmri_topo_rele(thp_cur);
		}
	}
	if (pathname)
		free(pathname);		/* done with pathname */

	fmd_dprintf(FMD_DBG_TOPO, "%s: cna_dev_ev=0x%llx, "
	    "cna_dev_topo=0x%llx\n", __func__, cna_dev_ev, cna_dev_topo);

	if (cna_dev_ev && cna_dev_topo &&
	    (cna_dev_ev <= cna_dev_topo))
		return (1);		/* filtered */
	return (0);			/* not filtered */
}

void
fmd_dr_event(sysevent_t *ev, fmd_topo_t **ftpp)
{
	uint64_t	gen;
	fmd_event_t	*e;
	fmd_topo_t	*ftp = NULL;
	hrtime_t	hrtime_dr;

	/* Filter events that should not trigger a new topo snapshot */
	if (filtered_by_class(ev)) {
		(void) pthread_mutex_lock(&fmd.d_stats_lock);
		fmd.d_stats->ds_dr_filter_class.fmds_value.ui64++;
		(void) pthread_mutex_unlock(&fmd.d_stats_lock);
		fmd_dprintf(FMD_DBG_TOPO, "%s: filtered by class\n", __func__);
		return;			/* class filtered event */
	}

	/* If we are simulating time, don't filter by time. */
	if (filtered_by_time(ev) &&
	    (fmd.d_clockops == &fmd_timeops_native)) {
		(void) pthread_mutex_lock(&fmd.d_stats_lock);
		fmd.d_stats->ds_dr_filter_time.fmds_value.ui64++;
		(void) pthread_mutex_unlock(&fmd.d_stats_lock);
		fmd_dprintf(FMD_DBG_TOPO, "%s: filtered by time\n", __func__);
		return;			/* time filtered event */
	}

	if (filtered_by_cna(ev)) {
		(void) pthread_mutex_lock(&fmd.d_stats_lock);
		fmd.d_stats->ds_dr_filter_cna_dev.fmds_value.ui64++;
		(void) pthread_mutex_unlock(&fmd.d_stats_lock);
		fmd_dprintf(FMD_DBG_TOPO, "%s: filtered by cna\n", __func__);
		return;			/* cna filtered event */
	}

	sysevent_get_time(ev, &hrtime_dr);

	/*
	 * Committed to taking a new topo snapshot: increment DR generation,
	 * take a new topo snapshot, and notify modules that a configuration
	 * change has occurred.
	 */
	(void) pthread_mutex_lock(&fmd.d_stats_lock);
	gen = fmd.d_stats->ds_dr_gen.fmds_value.ui64++;
	(void) pthread_mutex_unlock(&fmd.d_stats_lock);
	TRACE((FMD_DBG_EVT, "dr event %p, gen=%llu", (void *)ev, gen));

	/*
	 * Prevent non-transport modules from consuming any new events
	 * until the new topology snapshot is done. Modules will block
	 * on this event until we release it below.
	 */
	if (!FMD_DEBUG_TOPO_NO_UPDATE_DELAY) {
		fmd.d_mod_dr_topo_event = e = fmd_event_create(FMD_EVT_CTL,
		    hrtime_dr, NULL, fmd_ctl_init(NULL));
		fmd_dprintf(FMD_DBG_TOPO, "%s: created control event for "
		    "topo update delay, e=0x%p\n", __func__, (void *)e);
		fmd_event_hold(e);
		fmd_modhash_dispatch(fmd.d_mod_hash, e,
		    FMD_MOD_DISP_NONXPRT, NULL);
		fmd_dprintf(FMD_DBG_TOPO, "%s: dispatched control event "
		    "to all non-transport modules, e=0x%p\n",
		    __func__, (void *)e);
	}

	/*
	 * If debug option is set, then look for and load a debug
	 * snapshot otherwise generate a new snapshot as usual.
	 */
	if (FMD_DEBUG_TOPO_UPDATE_LOAD) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: loading new snapshot\n",
		    __func__);
		ftp = fmd_topo_load(TOPO_SNAPSHOT_LINK_DR);
		if (ftp == NULL) {
			fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
			    "%s: failed to load new snapshot\n", __func__);
		}
	}
	if (ftp == NULL) {
		fmd_dprintf(FMD_DBG_TOPO, "%s: creating new snapshot\n",
		    __func__);
		ftp = fmd_topo_update();
		if (ftp == NULL) {
			fmd_dprintf(FMD_DBG_ERR | FMD_DBG_TOPO,
			    "%s: failed to create new snapshot\n", __func__);
			goto exit;
		}
	}

	fmd_dprintf(FMD_DBG_TOPO, "%s: ftp=0x%p, thp=0x%p, uuid=%s\n",
	    __func__, (void *)ftp, (void *)ftp->ft_hdl,
	    topo_hdl_uuid(ftp->ft_hdl));

	/* Save the timestamp of the DR event. */
	ftp->ft_time_dr = hrtime_dr;

	fmd_asru_al_hash_apply(fmd.d_asrus, fmd_asru_handle_dr, NULL);

	/*
	 * Dispatch the topo event using the same timestamp as the control
	 * event above so that it will be queued immediately after it.
	 */
	e = fmd_event_create(FMD_EVT_TOPO, hrtime_dr, NULL, ftp);
	fmd_modhash_dispatch(fmd.d_mod_hash, e, NULL, NULL);

	fmd.d_topo_previous = fmd.d_topo_current;
	fmd.d_topo_current = ftp;

exit:
	/*
	 * If we paused modules above, then unpause them now.
	 */
	if (!FMD_DEBUG_TOPO_NO_UPDATE_DELAY &&
	    (fmd.d_mod_dr_topo_event != NULL)) {
		fmd_dprintf(FMD_DBG_TOPO, "%s releasing delay event e=0x%p\n",
		    __func__, (void *)fmd.d_mod_dr_topo_event);
		fmd_event_rele(fmd.d_mod_dr_topo_event);
		fmd.d_mod_dr_topo_event = NULL;
	}

	*ftpp = ftp;
}