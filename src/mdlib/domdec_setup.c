#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include "domdec.h"
#include "calcgrid.h"
#include "network.h"
#include "perf_est.h"
#include "physics.h"
#include "smalloc.h"
#include "typedefs.h"
#include "vec.h"

/* Margin for setting up the DD grid */
#define DD_GRID_MARGIN_PRES_SCALE 1.05

static int factorize(int n,int **fac,int **mfac)
{
  int d,ndiv;

  /* Decompose n in factors */
  snew(*fac,n/2);
  snew(*mfac,n/2);
  d = 2;
  ndiv = 0;
  while (n > 1) {
    while (n % d == 0) {
      if (ndiv == 0 || (*fac)[ndiv-1] != d) {
	ndiv++;
	(*fac)[ndiv-1] = d;
      }
      (*mfac)[ndiv-1]++;
      n /= d;
    }
    d++;
  }

  return ndiv;
}

static bool fits_perf(FILE *fplog,
		      t_inputrec *ir,matrix box,t_topology *top,
		      int nnodes,int npme,float ratio)
{
  bool bFits;
  int nkx,nky;
  t_inputrec try;
  float ratio_new;

  bFits = FALSE;
  if (ir->nkx % npme == 0 && ir->nky % npme == 0) {
    bFits = ((double)npme/(double)nnodes > 0.95*ratio);
  } else {
    /* Try enlarging the PME grid */
    if (compatible_pme_nx_ny(ir,npme,&nkx,&nky)) {
      if (nkx*nky <= pme_grid_enlarge_limit()*ir->nkx*ir->nky) {
	/* The computational cost of pme_load_estimate is order natoms,
	 * so we should avoid calling it when possible.
	 * The new ratio will always be larger than the old one,
	 * so we might save time by first checking with the old ratio.
	 */
	if ((double)npme/(double)nnodes > 0.95*ratio) {
	  try = *ir;
	  try.nkx = nkx;
	  try.nky = nky;
	  ratio_new = pme_load_estimate(top,&try,box);
	  bFits = ((double)npme/(double)nnodes > 0.95*ratio_new);
	  if (bFits) {
	    change_pme_grid(fplog,TRUE,npme,ir,nkx,nky);
	  }
	}
      }
    }
  }

  return bFits;
}

static int guess_npme(FILE *fplog,t_topology *top,t_inputrec *ir,matrix box,
		      int nnodes)
{
  float ratio;
  int  npme,nkx,nky,ndiv,*div,*mdiv,ldiv;
  t_inputrec try;

  ratio = pme_load_estimate(top,ir,box);

  if (fplog)
    fprintf(fplog,"Guess for relative PME load: %.2f\n",ratio);

  /* We assume the optimal node ratio is close to the load ratio.
   * The communication load is neglected,
   * but (hopefully) this will balance out between PP and PME.
   */

  /* First try to find npme as a factor of nnodes up to nnodes/3 */
  npme = 1;
  while (npme <= nnodes/3) {
    if (nnodes % npme == 0) {
      /* Note that fits_perf may change the PME grid */
      if (fits_perf(fplog,ir,box,top,nnodes,npme,ratio))
	break;
    }
    npme++;
  }
  if (npme > nnodes/3) {
    /* Try any possible number for npme */
    npme = 1;
    while (npme <= nnodes/2) {
      ndiv = factorize(nnodes-npme,&div,&mdiv);
      ldiv = div[ndiv-1];
      sfree(div);
      sfree(mdiv);
      /* Only use this value if nnodes-npme does not have
       * a large prime factor (5 y, 7 n, 14 n, 15 y).
       */
      if (ldiv <= 3 + (int)(pow(nnodes-npme,1.0/3.0) + 0.5)) {
	/* Note that fits_perf may change the PME grid */
	if (fits_perf(fplog,ir,box,top,nnodes,npme,ratio))
	  break;
      }
      npme++;
    }
  }
  if (npme > nnodes/2) {
    if (ir->nkx % nnodes || ir->nky % nnodes) {
      gmx_fatal(FARGS,"Could not find an appropriate number of separate PME nodes that is a multiple of the fourier grid x (%d) and y (%d) points, even when trying up to %d%% more grid points.\n"
	      "Use the -npme option of mdrun or change the number of processors or the grid dimensions.",
	      ir->nkx,ir->nky,(int)(100*(pme_grid_enlarge_limit()-1)));
    }
    npme = 0;
  } else {
    if (fplog)
      fprintf(fplog,
	      "Will use %d particle-particle and %d PME only nodes\n"
	      "This is a guess, check the performance at the end of the log file\n",
	      nnodes-npme,npme);
    fprintf(stderr,"\n"
	    "Will use %d particle-particle and %d PME only nodes\n"
	    "This is a guess, check the performance at the end of the log file\n",
	    nnodes-npme,npme);
  }

  return npme;
}

static int lcd(int n1,int n2)
{
  int d,i;
  
  d = 1;
  for(i=2; (i<=n1 && i<=n2); i++) {
    if (n1 % i == 0 && n2 % i == 0)
      d = i;
  }

  return d;
}

static float comm_cost_est(gmx_domdec_t *dd,real limit,real cutoff,
			   matrix box,t_inputrec *ir,float pbcdxr,
			   int npme,ivec nc)
{
  int  i,j,k,npp;
  rvec bt,nw;
  float comm_vol,comm_vol_pme,cost_pbcdx;
  /* This is the cost of a pbc_dx call relative to the cost
   * of communicating the coordinate and force of an atom.
   * This will be machine dependent.
   * These factors are for x86 with SMP or Infiniband.
   */
  float pbcdx_rect_fac = 0.1;
  float pbcdx_tric_fac = 0.2;

  /* Check the DD algorithm restrictions */
  if ((ir->ePBC == epbcXY && nc[ZZ] > 1) ||
      (ir->ePBC == epbcSCREW && (nc[XX] == 1 || nc[YY] > 1 || nc[ZZ] > 1)))
    return -1;
  
  /* Check if the triclinic requirements are met */
  for(i=0; i<DIM; i++) {
    for(j=i+1; j<DIM; j++) {
      if (box[j][i] != 0) {
	if (nc[j] > 1 && nc[i] == 1)
	  return -1;
      }
    }
  }

  for(i=0; i<DIM; i++) {
    bt[i] = box[i][i]*dd->skew_fac[i];
    nw[i] = nc[i]*cutoff/bt[i];

    if (bt[i] < nc[i]*limit)
      return -1;
  }
    
  /* When two dimensions are (nearly) equal, use more cells
   * for the smallest index, so the decomposition does not
   * depend sensitively on the rounding of the box elements.
   */
  for(i=0; i<DIM; i++) {
    if (npme == 0 || i != XX) {
      for(j=i+1; j<DIM; j++) {
	if (fabs(bt[j] - bt[i]) < 0.01*bt[i] && nc[j] > nc[i])
	  return -1;
      }
    }
  }

  npp = 1;
  comm_vol = 0;
  for(i=0; i<DIM; i++) {
    if (nc[i] > 1) {
      npp *= nc[i];
      comm_vol += nw[i];
      for(j=i+1; j<DIM; j++) {
	if (nc[j] > 1) {
	  comm_vol += nw[i]*nw[j]*M_PI/4;
	  for(k=j+1; k<DIM; k++) {
	    if (nc[k] > 1) {
	      comm_vol += nw[i]*nw[j]*nw[k]*M_PI/6;
	    }
	  }
	}
      }
    }
  }
  /* Normalize of the number of PP nodes */
  comm_vol /= npp;

  /* Determine the largest volume that a PME only needs to communicate */
  comm_vol_pme = 0;
  if ((npme > 0) && (nc[XX] % npme != 0)) {
    if (nc[XX] > npme) {
      comm_vol_pme = (npme==2 ? 1.0/3.0 : 0.5);
    } else {
      comm_vol_pme = 1.0 - lcd(nc[XX],npme)/(double)npme;
    }
    /* Normalize the number of PME only nodes */
    comm_vol_pme /= npme;
  }

  /* Add cost of pbc_dx for bondeds */
  cost_pbcdx = 0;
  if ((nc[XX] == 1 || nc[YY] == 1) || (nc[ZZ] == 1 && ir->ePBC != epbcXY)) {
    if ((dd->tric_dir[XX] && nc[XX] == 1) ||
	(dd->tric_dir[YY] && nc[YY] == 1))
      cost_pbcdx = pbcdxr*pbcdx_tric_fac/npp;
    else
      cost_pbcdx = pbcdxr*pbcdx_rect_fac/npp;
  }

  if (debug)
    fprintf(debug,
	    "nc %2d %2d %2d vol pp %6.4f pbcdx %6.4f pme %6.4f tot %6.4f\n",
	    nc[XX],nc[YY],nc[ZZ],
	    comm_vol,cost_pbcdx,comm_vol_pme,
	    comm_vol + cost_pbcdx + comm_vol_pme);

  return comm_vol + cost_pbcdx + comm_vol_pme;
}

static void assign_factors(gmx_domdec_t *dd,
			   real limit,real cutoff,
			   matrix box,t_inputrec *ir,float pbcdxr,int npme,
			   int ndiv,int *div,int *mdiv,ivec try,ivec opt)
{
  int x,y,z,i;
  float ce;

  if (ndiv == 0) {
    ce = comm_cost_est(dd,limit,cutoff,box,ir,pbcdxr,npme,try);
    if (ce >= 0 && (opt[XX] == 0 ||
		    ce < comm_cost_est(dd,limit,cutoff,box,ir,pbcdxr,
				       npme,opt)))
      copy_ivec(try,opt);

    return;
  }

  for(x=mdiv[0]; x>=0; x--) {
    for(i=0; i<x; i++)
      try[XX] *= div[0];
    for(y=mdiv[0]-x; y>=0; y--) {
      for(i=0; i<y; i++)
	try[YY] *= div[0];
      for(i=0; i<mdiv[0]-x-y; i++)
	try[ZZ] *= div[0];
      
      /* recurse */
      assign_factors(dd,limit,cutoff,box,ir,pbcdxr,npme,
		     ndiv-1,div+1,mdiv+1,try,opt);
      
      for(i=0; i<mdiv[0]-x-y; i++)
	try[ZZ] /= div[0];
      for(i=0; i<y; i++)
	try[YY] /= div[0];
    }
    for(i=0; i<x; i++)
      try[XX] /= div[0];
  }
}

static void optimize_ncells(FILE *fplog,
			    int nnodes_tot,int npme_only,real dlb_scale,
			    t_topology *top,matrix box,t_inputrec *ir,
			    gmx_domdec_t *dd,
			    real cellsize_limit,real cutoff_mbody,
			    bool bInterCGBondeds,bool bInterCGMultiBody,
			    ivec nc)
{
  int npp,npme,ndiv,*div,*mdiv,d;
  bool bExcl_pbcdx,bC;
  float pbcdxr;
  real limit,cutoff;
  ivec try;
  char buf[STRLEN];
  
  limit  = cutoff_mbody;
  cutoff = max(max(ir->rlist,max(ir->rcoulomb,ir->rvdw)),cutoff_mbody);

  dd->nc[XX] = 1;
  dd->nc[YY] = 1;
  dd->nc[ZZ] = 1;
  dd_set_tric_dir(dd,box);

  npp = nnodes_tot - npme_only;
  if (EEL_PME(ir->coulombtype)) {
    npme = (npme_only > 0 ? npme_only : npp);
  } else {
    npme = 0;
  }

  if (bInterCGBondeds) {
    /* For Ewald exclusions pbc_dx is not called */
    bExcl_pbcdx =
      (EEL_EXCL_FORCES(ir->coulombtype) && !EEL_FULL(ir->coulombtype));
    pbcdxr = (double)n_bonded_dx(top,bExcl_pbcdx)/(double)top->atoms.nr;
    
    if (bInterCGMultiBody && limit <= 0) {
      /* Here we should determine the minimum cell size from
       * the largest cg COG distance between atoms involved
       * in bonded interactions.
       */
      /* Set lower limit for the cell size to half the cut-off */
      limit = cutoff/2;
    }
    /* Take the maximum of the bonded and constraint distance limit */
    limit = max(limit,cellsize_limit);
  } else {
    /* Every molecule is a single charge group: no pbc required */
    pbcdxr = 0;
  }
  /* Add a margin for DLB and/or pressure scaling */
  if (dd->bDynLoadBal) {
    if (dlb_scale >= 1.0)
      gmx_fatal(FARGS,"The value for option -dds should be smaller than 1");
    if (fplog)
      fprintf(fplog,"Scaling the initial minimum size with 1/%g (option -dds) = %g\n",dlb_scale,1/dlb_scale);
    limit /= dlb_scale;
  } else if (ir->epc != epcNO) {
    if (fplog)
      fprintf(fplog,"To account for pressure scaling, scaling the initial minimum size with %g\n",DD_GRID_MARGIN_PRES_SCALE);
    limit *= DD_GRID_MARGIN_PRES_SCALE;
  }

  if (fplog) {
    fprintf(fplog,"Optimizing the DD grid for %d cells with a minimum initial size of %.3f nm\n",npp,limit);
    if (limit > 0) {
      fprintf(fplog,"The maximum allowed number of cells is:");
      for(d=0; d<DIM; d++)
	fprintf(fplog," %c %d",
		'X' + d,
		(d == ZZ && ir->ePBC == epbcXY && ir->nwall < 2) ? 1 :
		(int)(box[d][d]*dd->skew_fac[d]/limit));
      fprintf(fplog,"\n");
    }
  }

  if (debug)
    fprintf(debug,"Average nr of pbc_dx calls per atom %.2f\n",pbcdxr);

  /* Decompose npp in factors */
  ndiv = factorize(npp,&div,&mdiv);

  try[XX] = 1;
  try[YY] = 1;
  try[ZZ] = 1;
  clear_ivec(nc);
  assign_factors(dd,limit,cutoff,box,ir,pbcdxr,npme,ndiv,div,mdiv,try,nc);

  sfree(div);
  sfree(mdiv);

  if (nc[XX] == 0) {
    bC = (dd->bInterCGcons && cutoff_mbody < cellsize_limit);
    sprintf(buf,"Change the number of nodes or mdrun option %s%s%s",
	    !bC ? "-rdd" : "-rcon",
	    dd->bDynLoadBal ? " or -dds" : "",
	    bC ? " or your LINCS settings" : "");
    gmx_fatal(FARGS,"There is no domain decomposition for %d nodes that is compatible with the given box and a minimum cell size of %g nm\n"
	      "%s\n"
	      "Look in the log file for details on the domain decomposition",
	      npp,limit,buf);
  }
}

void dd_choose_grid(FILE *fplog,
		    t_commrec *cr,gmx_domdec_t *dd,t_inputrec *ir,
		    t_topology *top,matrix box,real dlb_scale,
		    real cellsize_limit,real cutoff_mbody,
		    bool bInterCGBondeds,bool bInterCGMultiBody)
{
  int npme,nkx,nky;

  if (MASTER(cr)) {
    if (EEL_PME(ir->coulombtype)) {
      if (cr->npmenodes >= 0) {
	/* Adapt the PME grid when it is not compatible. */
	npme = (cr->npmenodes>0 ? cr->npmenodes : cr->nnodes);
	make_compatible_pme_grid(fplog,MASTER(cr),npme,ir);
	if (ir->nkx % npme || ir->nky % npme) {
	  gmx_fatal(FARGS,"The number of fourier grid x (%d) and y (%d) points, is not a multiple of the number of nodes doing PME (%d), even when trying up to %d%% more grid points.",
		    ir->nkx,ir->nky,npme,
		    (int)(100*(pme_grid_enlarge_limit()-1)));
	}
      } else {
	/* We need to choose the number of PME only nodes */
	/* With less than 12 nodes we prefer no PME only nodes.
	 * try to adapt the PME grid when it is not compatible.
	 */
	if (cr->nnodes < 12) {
	  make_compatible_pme_grid(fplog,MASTER(cr),cr->nnodes,ir);
	}
	/* We assign PME only nodes with 12 or more nodes,
	 * or when the PME grid does not match the number of nodes.
	 */
	if (cr->nnodes > 2 && (cr->nnodes >= 12 ||
			       ir->nkx % cr->nnodes || ir->nky % cr->nnodes)) {
	  cr->npmenodes = guess_npme(fplog,top,ir,box,cr->nnodes);
	} else {
	  cr->npmenodes = 0;
	}
      }
    } else {
      if (cr->npmenodes < 0)
	cr->npmenodes = 0;
    }
    
    optimize_ncells(fplog,cr->nnodes,cr->npmenodes,dlb_scale,
		    top,box,ir,dd,
		    cellsize_limit,cutoff_mbody,
		    bInterCGBondeds,bInterCGMultiBody,
		    dd->nc);
  }
  /* Communicate the information set by the master to all nodes */
  gmx_bcast(sizeof(dd->nc),dd->nc,cr);
  if (EEL_PME(ir->coulombtype)) {
    gmx_bcast(sizeof(ir->nkx),&ir->nkx,cr);
    gmx_bcast(sizeof(ir->nky),&ir->nky,cr);
    gmx_bcast(sizeof(cr->npmenodes),&cr->npmenodes,cr);
  } else {
    cr->npmenodes = 0;
  }
}