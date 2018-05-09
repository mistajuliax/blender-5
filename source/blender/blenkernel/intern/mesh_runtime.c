/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2005 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Blender Foundation.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/mesh_runtime.c
 *  \ingroup bke
 */

#include "atomic_ops.h"

#include "MEM_guardedalloc.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_math_geom.h"
#include "BLI_threads.h"

#include "BKE_mesh.h"


static ThreadRWMutex loops_cache_lock = PTHREAD_RWLOCK_INITIALIZER;

/**
 * Default values defined at read time.
 */
void BKE_mesh_runtime_reset(Mesh *mesh)
{
	memset(&mesh->runtime, 0, sizeof(mesh->runtime));
}

/* This is a ported copy of DM_ensure_looptri_data(dm) */
/**
 * Ensure the array is large enough
 *
 * /note This function must always be thread-protected by caller. It should only be used by internal code.
 */
static void mesh_ensure_looptri_data(Mesh *mesh)
{
	const unsigned int totpoly = mesh->totpoly;
	const int looptris_len = poly_to_tri_count(totpoly, mesh->totloop);

	BLI_assert(mesh->runtime.looptris.array_wip == NULL);

	SWAP(MLoopTri *, mesh->runtime.looptris.array, mesh->runtime.looptris.array_wip);

	if ((looptris_len > mesh->runtime.looptris.len_alloc) ||
	    (looptris_len < mesh->runtime.looptris.len_alloc * 2) ||
	    (totpoly == 0))
	{
		MEM_SAFE_FREE(mesh->runtime.looptris.array_wip);
		mesh->runtime.looptris.len_alloc = 0;
		mesh->runtime.looptris.len = 0;
	}

	if (totpoly) {
		if (mesh->runtime.looptris.array_wip == NULL) {
			mesh->runtime.looptris.array_wip = MEM_malloc_arrayN(looptris_len, sizeof(*mesh->runtime.looptris.array_wip), __func__);
			mesh->runtime.looptris.len_alloc = looptris_len;
		}

		mesh->runtime.looptris.len = looptris_len;
	}
}

/* This is a ported copy of CDDM_recalc_looptri(dm). */
void BKE_mesh_runtime_looptri_recalc(Mesh *mesh)
{
	mesh_ensure_looptri_data(mesh);
	BLI_assert(mesh->totpoly == 0 || mesh->runtime.looptris.array_wip != NULL);

	BKE_mesh_recalc_looptri(
	        mesh->mloop, mesh->mpoly,
	        mesh->mvert,
	        mesh->totloop, mesh->totpoly,
	        mesh->runtime.looptris.array_wip);

	BLI_assert(mesh->runtime.looptris.array == NULL);
	atomic_cas_ptr((void **)&mesh->runtime.looptris.array, mesh->runtime.looptris.array, mesh->runtime.looptris.array_wip);
	mesh->runtime.looptris.array_wip = NULL;
}

/* This is a ported copy of dm_getNumLoopTri(dm). */
int BKE_mesh_runtime_looptri_len(const Mesh *mesh)
{
	const int looptri_len = poly_to_tri_count(mesh->totpoly, mesh->totloop);
	BLI_assert(ELEM(mesh->runtime.looptris.len, 0, looptri_len));
	return looptri_len;
}

/* This is a ported copy of dm_getLoopTriArray(dm). */
const MLoopTri *BKE_mesh_runtime_looptri_ensure(Mesh *mesh)
{
	MLoopTri *looptri;

	BLI_rw_mutex_lock(&loops_cache_lock, THREAD_LOCK_READ);
	looptri = mesh->runtime.looptris.array;
	BLI_rw_mutex_unlock(&loops_cache_lock);

	if (looptri != NULL) {
		BLI_assert(BKE_mesh_runtime_looptri_len(mesh) == mesh->runtime.looptris.len);
	}
	else {
		BLI_rw_mutex_lock(&loops_cache_lock, THREAD_LOCK_WRITE);
		/* We need to ensure array is still NULL inside mutex-protected code, some other thread might have already
		 * recomputed those looptris. */
		if (mesh->runtime.looptris.array == NULL) {
			BKE_mesh_runtime_looptri_recalc(mesh);
		}
		looptri = mesh->runtime.looptris.array;
		BLI_rw_mutex_unlock(&loops_cache_lock);
	}
	return looptri;
}
