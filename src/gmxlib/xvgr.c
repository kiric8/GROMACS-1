/*
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * For more info, check our website at http://www.gromacs.org
 * 
 * And Hey:
 * GROningen Mixture of Alchemy and Childrens' Stories
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <ctype.h>
#include "sysstuff.h"
#include "string2.h"
#include "futil.h"
#include "statutil.h"
#include "copyrite.h"
#include "smalloc.h"
#include "xvgr.h"
#include "viewit.h"
#include "vec.h"
#include "gmxfio.h"

bool use_xmgr()
{
  char *env;
  bool bXMGR;

  env = getenv("GMX_VIEW_XVG");

  return (env!=NULL && strcmp(env,"xmgr")==0);
} 

FILE *xvgropen(const char *fn,const char *title,const char *xaxis,const char *yaxis)
{
  FILE *xvgr;
  char pukestr[100];
  time_t t;
  
  xvgr=gmx_fio_fopen(fn,"w");
  if (bPrintXvgrCodes()) {
    time(&t);
    fprintf(xvgr,"# This file was created %s",ctime(&t));
    fprintf(xvgr,"# by the following command:\n# %s\n#\n",command_line());
    fprintf(xvgr,"# %s is part of G R O M A C S:\n#\n",Program());
    bromacs(pukestr,99);
    fprintf(xvgr,"# %s\n#\n",pukestr);
    fprintf(xvgr,"@    title \"%s\"\n",title);
    fprintf(xvgr,"@    xaxis  label \"%s\"\n",xaxis);
    fprintf(xvgr,"@    yaxis  label \"%s\"\n",yaxis);
    if (use_xmgr())
      fprintf(xvgr,"@TYPE nxy\n");
    else
      fprintf(xvgr,"@TYPE xy\n");
  }
  return xvgr;
}

void
xvgrclose(FILE *fp)
{
	gmx_fio_fclose(fp);
}

void xvgr_subtitle(FILE *out,const char *subtitle)
{
  if (bPrintXvgrCodes()) 
    fprintf(out,"@ subtitle \"%s\"\n",subtitle);
}

void xvgr_view(FILE *out,real xmin,real ymin,real xmax,real ymax)
{
  if (bPrintXvgrCodes()) 
    fprintf(out,"@ view %g, %g, %g, %g\n",xmin,ymin,xmax,ymax);
}

void xvgr_world(FILE *out,real xmin,real ymin,real xmax,real ymax)
{
  if (bPrintXvgrCodes()) 
    fprintf(out,"@ world xmin %g\n"
	    "@ world ymin %g\n"
	    "@ world xmax %g\n"
	    "@ world ymax %g\n",xmin,ymin,xmax,ymax);
}

void xvgr_legend(FILE *out,int nsets,char **setname)
{
  int i;
  
  if (bPrintXvgrCodes()) {
    xvgr_view(out,0.15,0.15,0.75,0.85);
    fprintf(out,"@ legend on\n");
    fprintf(out,"@ legend box on\n");
    fprintf(out,"@ legend loctype view\n");
    fprintf(out,"@ legend %g, %g\n",0.78,0.8);
    fprintf(out,"@ legend length %d\n",2);
    for(i=0; (i<nsets); i++)
      if (setname[i]) {
	if (use_xmgr())
	  fprintf(out,"@ legend string %d \"%s\"\n",i,setname[i]);
	else
	  fprintf(out,"@ s%d legend \"%s\"\n",i,setname[i]);
      }
  }
}

void xvgr_line_props(FILE *out, int NrSet, int LineStyle, int LineColor)
{
  if (bPrintXvgrCodes()) {
    fprintf(out, "@    with g0\n");
    fprintf(out, "@    s%d linestyle %d\n", NrSet, LineStyle);
    fprintf(out, "@    s%d color %d\n", NrSet, LineColor);
  }
}

static const char *LocTypeStr[] = { "view", "world" };
static const char *BoxFillStr[] = { "none", "color", "pattern" };
 
void xvgr_box(FILE *out,
	      int LocType,
	      real xmin,real ymin,real xmax,real ymax,
	      int LineStyle,int LineWidth,int LineColor,
	      int BoxFill,int BoxColor,int BoxPattern)
{
  if (bPrintXvgrCodes()) {
    fprintf(out,"@with box\n");
    fprintf(out,"@    box on\n");
    fprintf(out,"@    box loctype %s\n",LocTypeStr[LocType]);
    fprintf(out,"@    box %g, %g, %g, %g\n",xmin,ymin,xmax,ymax);
    fprintf(out,"@    box linestyle %d\n",LineStyle);
    fprintf(out,"@    box linewidth %d\n",LineWidth);
    fprintf(out,"@    box color %d\n",LineColor);
    fprintf(out,"@    box fill %s\n",BoxFillStr[BoxFill]);
    fprintf(out,"@    box fill color %d\n",BoxColor);
    fprintf(out,"@    box fill pattern %d\n",BoxPattern);
    fprintf(out,"@box def\n");
  }
}

static char *fgets3(FILE *fp,char ptr[],int *len)
{
  char *p;
  int  slen;

  if (fgets(ptr,*len-1,fp) == NULL)
    return NULL;
  p = ptr;
  while ((strchr(ptr,'\n') == NULL) && (!feof(fp))) {
    /* This line is longer than len characters, let's increase len! */
    *len += STRLEN;
    p    += STRLEN;
    srenew(ptr,*len);
    if (fgets(p-1,STRLEN,fp) == NULL)
      break;
  }
  slen = strlen(ptr);
  if (ptr[slen-1] == '\n')
    ptr[slen-1] = '\0';

  return ptr;
}

static int wordcount(char *ptr)
{
  int i,n,is[2];
  int cur=0;
#define prev (1-cur)
  
  if (strlen(ptr) == 0)
    return 0;
  /* fprintf(stderr,"ptr='%s'\n",ptr); */
  n=1;
  for(i=0; (ptr[i] != '\0'); i++) {
    is[cur] = isspace(ptr[i]);
    if ((i > 0)  && (is[cur] && !is[prev]))
      n++;
    cur=prev;
  }
  return n;
}

int read_xvg_legend(const char *fn,double ***y,int *ny,char ***legend)
{
  FILE   *fp;
  char   *ptr,*ptr0,*ptr1;
  char   *base=NULL;
  char   *fmt=NULL;
  int    k,line=0,nny,nx,maxx,rval,legend_nalloc,set,nchar;
  double lf;
  double **yy=NULL;
  char  *tmpbuf;
  int    len=STRLEN;
  *ny  = 0;
  nny  = 0;
  nx   = 0;
  maxx = 0;
  fp   = gmx_fio_fopen(fn,"r");

  snew(tmpbuf,len);
  legend_nalloc = 0;
  if (legend != NULL) {
    *legend = NULL;
  }

  while ((ptr = fgets3(fp,tmpbuf,&len)) != NULL && ptr[0]!='&') {
    line++;
    trim(ptr);
    if (ptr[0] == '@') {
      if (legend != NULL) {
	ptr++;
	trim(ptr);
	set = -1;
	if (strncmp(ptr,"legend string",13) == 0) {
	  ptr += 13;
	  sscanf(ptr,"%d%n",&set,&nchar);
	  ptr += nchar;
	} else if (ptr[0] == 's') {
	  ptr++;
	  sscanf(ptr,"%d%n",&set,&nchar);
	  ptr += nchar;
	  trim(ptr);
	  if (strncmp(ptr,"legend",6) == 0) {
	    ptr += 6;
	  } else {
	    set = -1;
	  }
	}
	if (set >= 0) {
	  if (set >= legend_nalloc) {
	    legend_nalloc = set + 1;
	    srenew(*legend,legend_nalloc);
	    ptr0 = strchr(ptr,'"');
	    if (ptr0 != NULL) {
	      ptr0++;
	      ptr1 = strchr(ptr0,'"');
	      if (ptr1 != NULL) {
		(*legend)[set] = strdup(ptr0);
		(*legend)[set][ptr1-ptr0] = '\0';
	      }
	    }
	  }
	}
      }
    } else if (ptr[0] != '#') {
      if (nny == 0) {
	(*ny) = nny = wordcount(ptr);
	/* fprintf(stderr,"There are %d columns in your file\n",nny);*/
	if (nny == 0)
	  return 0;
	snew(yy,nny);
	snew(fmt,3*nny+1);
	snew(base,3*nny+1);
      }
      /* Allocate column space */
      if (nx >= maxx) {
	maxx+=1024;
	for(k=0; (k<nny); k++)
	  srenew(yy[k],maxx);
      }
      /* Initiate format string */
      fmt[0]  = '\0';
      base[0] = '\0';
      
      /* fprintf(stderr,"ptr='%s'\n",ptr);*/
      for(k=0; (k<nny); k++) {
	strcpy(fmt,base);
	strcat(fmt,"%lf");
	rval = sscanf(ptr,fmt,&lf);
	/* fprintf(stderr,"rval = %d\n",rval);*/
	if ((rval == EOF) || (rval == 0))
	  break;
	yy[k][nx] = lf;
	srenew(fmt,3*(nny+1)+1);
	srenew(base,3*nny+1);
	strcat(base,"%*s");
      }
      if (k != nny) {
	fprintf(stderr,"Only %d columns on line %d in file %s\n",
		k,line,fn);
	for( ; (k<nny); k++)
	  yy[k][nx] = 0.0;
      }
      nx++;
    }
  }
  gmx_fio_fclose(fp);
  
  *y = yy;
  sfree(tmpbuf);

  if (legend_nalloc > 0) {
    if (*ny - 1 > legend_nalloc) {
      srenew(*legend,*ny-1);
      for(set=legend_nalloc; set<*ny-1; set++) {
	(*legend)[set] = NULL;
      }
    }
  }

  return nx;
}

int read_xvg(const char *fn,double ***y,int *ny)
{
  return read_xvg_legend(fn,y,ny,NULL);
}

void write_xvg(const char *fn,const char *title,int nx,int ny,real **y,
               char ** leg)
{
  FILE *fp;
  int  i,j;
  
  fp=xvgropen(fn,title,"X","Y");
  if (leg)
    xvgr_legend(fp,ny-1,leg);
  for(i=0; (i<nx); i++) {
    for(j=0; (j<ny); j++) {
      fprintf(fp,"  %12.5e",y[j][i]);
    }
    fprintf(fp,"\n");
  }
  xvgrclose(fp);
}

real **read_xvg_time(char *fn,
		     bool bHaveT,bool bTB,real tb,bool bTE,real te,
		     int nsets_in,int *nset,int *nval,real *dt,real **t)
{
  FILE   *fp;
#define MAXLINELEN 16384
  char line0[MAXLINELEN];
  char   *line;
  int    t_nalloc,*val_nalloc,a,narg,n,sin,set,nchar;
  double dbl,tend=0;
  bool   bEndOfSet,bTimeInRange,bFirstLine=TRUE;
  real   **val;
  
  t_nalloc = 0;
  *t  = NULL;
  val = NULL;
  val_nalloc = NULL;
  *nset = 0;
  *dt = 0;
  fp  = gmx_fio_fopen(fn,"r");
  for(sin=0; sin<nsets_in; sin++) {
    if (nsets_in == 1)
      narg = 0;
    else 
      narg = bHaveT ? 2 : 1;
    n = 0;
    bEndOfSet = FALSE;
    while (!bEndOfSet && fgets(line0,MAXLINELEN,fp)) {
      line = line0;
      /* Remove whitespace */
      while (line[0]==' ' || line[0]=='\t')
	line++;
      bEndOfSet = (line[0] == '&');
      if (line[0]!='#' && line[0]!='@' && line[0]!='\n' && !bEndOfSet) {
	if (bFirstLine && bHaveT) {
	  /* Check the first line that should contain data */
	  a = sscanf(line,"%lf%lf",&dbl,&dbl);
	  if (a == 0) 
	    gmx_fatal(FARGS,"Expected a number in %s on line:\n%s",fn,line0);
	  else if (a == 1) {
	    fprintf(stderr,"Found only 1 number on line, "
		    "assuming no time is present.\n");
	    bHaveT = FALSE;
	    if (nsets_in > 1)
	      narg = 1;
	  }
	}

	a = 0;
	bTimeInRange = TRUE;
	while ((a<narg || (nsets_in==1 && n==0)) && 
	       sscanf(line,"%lf%n",&dbl,&nchar)==1 && bTimeInRange) {
	  /* Use set=-1 as the time "set" */
	  if (sin) {
	    if (!bHaveT || (a>0))
	      set = sin;
	    else
	      set = -1;
	  } else {
	    if (!bHaveT)
	      set = a;
	    else
	      set = a-1;
	  }
	  if (set==-1 && ((bTB && dbl<tb) || (bTE && dbl>te)))
	    bTimeInRange = FALSE;
	    
	  if (bTimeInRange) {
	    if (n==0) {
	      if (nsets_in == 1)
		narg++;
	      if (set >= 0) {
		*nset = set+1;
		srenew(val,*nset);
		srenew(val_nalloc,*nset);
		val_nalloc[set] = 0;
		val[set] = NULL;
	      }
	    }
	    if (set == -1) {
	      if (sin == 0) {
		if (n >= t_nalloc) {
		  t_nalloc = over_alloc_small(n);
		  srenew(*t,t_nalloc);
		}
		(*t)[n] = dbl;
	      }
	      /* else we should check the time of the next sets with set 0 */
	    } else {
	      if (n >= val_nalloc[set]) {
		val_nalloc[set] = over_alloc_small(n);
		srenew(val[set],val_nalloc[set]);
	      }
	      val[set][n] = (real)dbl;
	    }
	  }
	  a++;
	  line += nchar;
	}
	if (line0[strlen(line0)-1] != '\n') {
	  fprintf(stderr,"File %s does not end with a newline, ignoring the last line\n",fn);
	} else if (bTimeInRange) {
	  if (a == 0) {
	    fprintf(stderr,"Ignoring invalid line in %s:\n%s",fn,line0);
	  } else {
	    if (a != narg)
	      fprintf(stderr,"Invalid line in %s:\n%s"
		      "Using zeros for the last %d sets\n",
		      fn,line0,narg-a);
	    n++;
	  }
	}
	if (a > 0)
	  bFirstLine = FALSE;
      }
    }
    if (sin==0) {
      *nval = n;
      if (!bHaveT) {
	snew(*t,n);
	for(a=0; a<n; a++)
	  (*t)[a] = a;
      }
      if (n > 1)
	*dt = (real)((*t)[n-1]-(*t)[0])/(n-1.0);
      else
	*dt = 1;
    } else {
      if (n < *nval) {
	fprintf(stderr,"Set %d is shorter (%d) than the previous set (%d)\n",
		sin+1,n,*nval);
	*nval = n;
	fprintf(stderr,"Will use only the first %d points of every set\n",
		*nval);
      }
    }
  }
  gmx_fio_fclose(fp);

  sfree(val_nalloc);
  
  return val;
}

