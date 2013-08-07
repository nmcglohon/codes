/*
 * Copyright (C) 2011, University of Chicago
 *
 * See COPYRIGHT notice in top-level directory.
 */

/* SUMMARY:
 * CODES custom mapping file for ROSS
 */
#include "codes/codes_mapping.h"

/* number of LPs assigned to the current PE (abstraction of MPI rank) */
static int lps_for_this_pe = 0;

/* char arrays for holding lp type name and group name*/
char local_grp_name[MAX_NAME_LENGTH], local_lp_name[MAX_NAME_LENGTH];

config_lpgroups_t lpconf;

int codes_mapping_get_lps_for_pe()
{
  return lps_for_this_pe;
}

/* Takes the global LP ID and returns the rank (PE id) on which the LP is mapped */
tw_peid codes_mapping( tw_lpid gid)
{
  return gid / lps_for_this_pe;
}

/* This function loads the configuration file and sets up the number of LPs on each PE */
void codes_mapping_setup()
{
  int grp, lpt;
  int pes = tw_nnodes();

  configuration_get_lpgroups(&config, "LPGROUPS", &lpconf);

  for (grp = 0; grp < lpconf.lpgroups_count; grp++)
   {
    for (lpt = 0; lpt < lpconf.lpgroups[grp].lptypes_count; lpt++)
	lps_for_this_pe += (lpconf.lpgroups[grp].lptypes[lpt].count * lpconf.lpgroups[grp].repetitions);
   }
  lps_for_this_pe /= pes;
 //printf("\n LPs for this PE are %d reps %d ", lps_for_this_pe,  lpconf.lpgroups[grp].repetitions);
}

/* This function takes the group ID , type ID and rep ID then returns the global LP ID */
/* TODO: Add string based search for LP group and type names */
void codes_mapping_get_lp_id(char* grp_name, char* lp_type_name, int rep_id, int offset, tw_lpid* gid)
{
 int grp, lpt, lpcount = 0, lp_types_count, rep, count_for_this_lpt;
 short found = 0;

 // Account for all lps in the previous groups 
 for(grp = 0; grp < lpconf.lpgroups_count; grp++)
  {
    lp_types_count = lpconf.lpgroups[grp].lptypes_count;
    rep = lpconf.lpgroups[grp].repetitions;

    if(strcmp(lpconf.lpgroups[grp].name, grp_name) == 0)
    {
	    found = 1;
	    break;
    }
    
    for(lpt = 0; lpt < lp_types_count; lpt++)
      lpcount += (rep * lpconf.lpgroups[grp].lptypes[lpt].count);
  }

 assert(found);
 found = 0;

 lp_types_count = lpconf.lpgroups[grp].lptypes_count;
 
 // Account for the previous lp types in the current repetition
 for(lpt = 0; lpt < lp_types_count; lpt++)
 {
   count_for_this_lpt = lpconf.lpgroups[grp].lptypes[lpt].count;

   if(strcmp(lpconf.lpgroups[grp].lptypes[lpt].name, lp_type_name) == 0)
   {
     found = 1;
     break;
   }
   
   lpcount += count_for_this_lpt;
 }

 assert(found);
 // Account for all previous repetitions
 for(rep = 0; rep < rep_id; rep++)
 {
    for(lpt = 0; lpt < lp_types_count; lpt++)
      lpcount += lpconf.lpgroups[grp].lptypes[lpt].count;
 }
   *gid = lpcount + offset;
}

/* This function takes the LP ID and returns its grp index, lp type ID and repetition ID */
void codes_mapping_get_lp_info(tw_lpid gid, char* grp_name, int* grp_id, int* lp_type_id, char* lp_type_name, int* grp_rep_id, int* offset)
{
  int grp, lpt, rep, grp_offset, lp_offset, rep_offset;
  int lp_tmp_id, lp_types_count, lp_count;
  unsigned long grp_lp_count=0;
  short found = 0;
 
  /* Find the group id first */ 
  for(grp = 0; grp < lpconf.lpgroups_count; grp++)
  {
    grp_offset = 0;
    rep_offset = 0;
    rep = lpconf.lpgroups[grp].repetitions;
    lp_types_count = lpconf.lpgroups[grp].lptypes_count;

    for(lpt = 0; lpt < lp_types_count; lpt++)
    {
	lp_count = lpconf.lpgroups[grp].lptypes[lpt].count;
	grp_offset += (rep * lp_count);
	rep_offset += lp_count;
    }
    /* Each gid is assigned an increasing number starting from 0th group and 0th lp type
     * so we check here if the gid lies within the numeric range of a group */ 
    if(gid >= grp_lp_count && gid < grp_lp_count + grp_offset)
    {
	*grp_id = grp;
	 strcpy(local_grp_name, lpconf.lpgroups[grp].name);
	 lp_offset = gid - grp_lp_count; /* gets the lp offset starting from the point where the group begins */
         *grp_rep_id = lp_offset / rep_offset;
          lp_tmp_id = lp_offset - (*grp_rep_id * rep_offset);
	  found = 1;
	 break;
    }
    grp_lp_count += grp_offset; /* keep on increasing the group lp count to next group range*/
  }
  assert(found);

  lp_offset = 0;
  found = 0; /* reset found for finding LP type */
 /* Now we compute the LP type ID here based on the lp offset that we just got */ 
  for(lpt = 0; lpt < lp_types_count; lpt++)
  {
     lp_count = lpconf.lpgroups[grp].lptypes[lpt].count;
     if(lp_tmp_id >= lp_offset && lp_tmp_id < lp_offset + lp_count)
     {
	     *lp_type_id = lpt;
	      strcpy(local_lp_name, lpconf.lpgroups[grp].lptypes[lpt].name);
	      *offset = lp_tmp_id - lp_offset;
	      found = 1;
     	      break;
     }
     lp_offset += lp_count;
  }
  assert(found);
  strncpy(grp_name, local_grp_name, MAX_NAME_LENGTH);
  strncpy(lp_type_name, local_lp_name, MAX_NAME_LENGTH);
  //printf("\n gid %d lp type name %s rep_id %d ", gid, lp_type_name, *grp_rep_id);
}

/* This function assigns local and global LP Ids to LPs */
void codes_mapping_init(void)
{
     int grp_id, lpt_id, rep_id, offset;
     tw_lpid ross_gid, ross_lid; /* ross global and local IDs */
     tw_pe * pe;
     char lp_type_name[MAX_NAME_LENGTH];
     char grp_name[MAX_NAME_LENGTH];
     int nkp_per_pe = g_tw_nkp;
     tw_lpid         lpid, kpid;

     /* have 16 kps per pe, this is the optimized configuration for ROSS custom mapping */
     for(kpid = 0; kpid < nkp_per_pe; kpid++)
	tw_kp_onpe(kpid, g_tw_pe[0]);

     int lp_init_range = g_tw_mynode * lps_for_this_pe;
     codes_mapping_get_lp_info(lp_init_range, grp_name, &grp_id, &lpt_id, lp_type_name, &rep_id, &offset);

     for (lpid = lp_init_range; lpid < lp_init_range + lps_for_this_pe; lpid++)
      {
	 ross_gid = lpid;
	 ross_lid = lpid - lp_init_range;
	 kpid = ross_lid % g_tw_nkp;
	 pe = tw_getpe(kpid % g_tw_npe);
	 codes_mapping_get_lp_info(ross_gid, grp_name, &grp_id, &lpt_id, lp_type_name, &rep_id, &offset);
	 tw_lp_onpe(ross_lid , pe, ross_gid);
	 tw_lp_onkp(g_tw_lp[ross_lid], g_tw_kp[kpid]);
	 tw_lp_settype(ross_lid, lp_type_lookup(lp_type_name));
     }
     return;
}

/* This function takes the global LP ID, maps it to the local LP ID and returns the LP 
 * lps have global and local LP IDs
 * global LP IDs are unique across all PEs, local LP IDs are unique within a PE */
tw_lp * codes_mapping_to_lp( tw_lpid lpid)
{
   int index = lpid - (g_tw_mynode * lps_for_this_pe);
//   printf("\n global id %d index %d lps_before %d lps_offset %d local index %d ", lpid, index, lps_before, g_tw_mynode, local_index);
   return g_tw_lp[index];
}

