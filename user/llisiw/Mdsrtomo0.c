/* Prestack first-arrival traveltime tomography (DSR) */
/*
  Copyright (C) 2013 University of Texas at Austin
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <rsf.h>

#include "dsreiko0.h"
#include "dsrtomo0.h"

int main(int argc, char* argv[])
{
    bool velocity, causal, limit, verb, shape;
    int dimw, dimt, i, n[SF_MAX_DIM], rect[SF_MAX_DIM], iw, nw, ir, nr;
    long nt, *order;
    int iter, niter, cgiter, count;
    int *ff, *dp, *mp, nloop;
    float o[SF_MAX_DIM], d[SF_MAX_DIM], *dt, *dw, *dv, *t, *w, *t0, *w1, *p=NULL;
    float eps, tol, thres, rhsnorm, rhsnorm0, rhsnorm1, rate, gama;
    char key[6];
    sf_file in, out, reco, grad, mask, prec;

    sf_init(argc,argv);
    in  = sf_input("in");
    out = sf_output("out");
   
    /* read dimension */
    dimw = sf_filedims(in,n);
	    
    nw = 1;
    for (i=0; i < dimw; i++) {
	sprintf(key,"d%d",i+1);
	if (!sf_histfloat(in,key,d+i)) sf_error("No %s= in input.",key);
	sprintf(key,"o%d",i+1);
	if (!sf_histfloat(in,key,o+i)) o[i]=0.;
	nw *= n[i];
    }
	    
    if (dimw > 2) sf_error("Only works for 2D now.");
	    
    n[2] = n[1]; d[2] = d[1]; o[2] = o[1];
    dimt = 3; nr = n[1]*n[2]; nt = nw*n[2];

    /* read initial velocity */
    w = sf_floatalloc(nw);
    sf_floatread(w,nw,in);
	    
    if (!sf_getbool("velocity",&velocity)) velocity=true;
    /* if y, the input is velocity; n, slowness-squared */
	    	    
    /* convert to slowness-squared */
    if (velocity) {
	for (iw=0; iw < nw; iw++)
	    w[iw] = 1./w[iw]*1./w[iw];
	
	dv = sf_floatalloc(nw);
    } else {
	dv = NULL;
    }
	    
    if (!sf_getbool("limit",&limit)) limit=false;
    /* if y, limit computation within receiver coverage */

    if (!sf_getbool("shape",&shape)) shape=false;
    /* shaping regularization (default no) */
	    
    /* read record */
    if (NULL == sf_getstring("reco"))
	sf_error("Need record reco=");
    reco = sf_input("reco");
    
    t0 = sf_floatalloc(nr);
    sf_floatread(t0,nr,reco);
    sf_fileclose(reco);
	    
    /* read receiver mask */	    
    if (NULL == sf_getstring("mask")) {
	mask = NULL;
	dp = NULL;
    } else {
	mask = sf_input("mask");
	dp = sf_intalloc(nr);
	sf_intread(dp,nr,mask);
	sf_fileclose(mask);
    }
	    
    /* read model mask */
    if (NULL == sf_getstring("prec")) {
	prec = NULL;
	mp = NULL;
    } else {
	prec = sf_input("prec");
	mp = sf_intalloc(nw);
	sf_intread(mp,nw,prec);
	sf_fileclose(prec);
    }

    if (!sf_getbool("verb",&verb)) verb=false;
    /* verbosity flag */
	    
    if (!sf_getint("niter",&niter)) niter=5;
    /* number of inversion iterations */
	    
    if (!sf_getint("cgiter",&cgiter)) cgiter=10;
    /* number of conjugate-gradient iterations */
	    
    if (!sf_getfloat("thres",&thres)) thres=5.e-5;
    /* threshold (percentage) */
	    
    if (!sf_getfloat("tol",&tol)) tol=1.e-3;
    /* tolerance for bisection root-search */

    if (!sf_getint("nloop",&nloop)) nloop=10;
    /* number of bisection root-search */

    /* output gradient at each iteration */
    if (NULL != sf_getstring("grad")) {
	grad = sf_output("grad");
	sf_putint(grad,"n3",niter);
    } else {
	grad = NULL;
    }
	    
    if (!sf_getfloat("eps",&eps)) eps=0.;
    /* regularization parameter */

    if (shape) {
	for (i=0; i < dimw; i++) {
	    sprintf(key,"rect%d",i+1);
	    if (!sf_getint(key,rect+i)) rect[i]=1;
	    /*( rect#=(1,1,...) smoothing radius on #-th axis )*/
	}
	
	/* triangle smoothing operator */
	sf_trianglen_init(dimw,rect,n);
	sf_repeat_init(nw,1,sf_trianglen_lop);
	
	sf_conjgrad_init(nw,nw,nr,nr,eps,1.e-6,verb,false);
	p = sf_floatalloc(nw);
    } else {
	/* initialize 2D gradient operator */
	sf_igrad2_init(n[0],n[1]);
    }

    /* allocate temporary array */
    t  = sf_floatalloc(nt);
    dw = sf_floatalloc(nw);
    dt = sf_floatalloc(nr);
    w1 = sf_floatalloc(nw);
    ff = sf_intalloc(nt);

    if (!sf_getbool("causal",&causal)) causal=true;
    /* if y, neglect non-causal branches of DSR */

    /* initialize eikonal */
    dsreiko_init(n,o,d,
		 thres,tol,nloop,
		 causal,limit,dp);
	    
    /* initialize operator */
    dsrtomo_init(dimt,n,d);

    /* upwind order */
    order = dsrtomo_order();

    /* initial misfit */
    dsreiko_fastmarch(t,w,ff,order);
    dsreiko_mirror(t);

    /* calculate L2 data-misfit */
    for (ir=0; ir < nr; ir++) {
	if (dp == NULL || dp[ir] == 1) {
	    dt[ir] = t0[ir]-t[(long) ir*n[0]];
	} else {
	    dt[ir] = 0.;
	}
    }

    rhsnorm0 = cblas_snrm2(nr,dt,1);
    rhsnorm = rhsnorm0;
    rhsnorm1 = rhsnorm;
    rate = rhsnorm1/rhsnorm0;
	    
    sf_warning("L2 misfit after iteration 0 of %d: %g",niter,rate);
    
    /* iterations over inversion */
    for (iter=0; iter < niter; iter++) {
	
	/* clean-up */
	for (iw=0; iw < nw; iw++) dw[iw] = 0.;
	
	/* set operator */
	dsrtomo_set(t,w,ff,dp,mp);
	
	/* solve dw */
	if (shape) {
	    sf_conjgrad(NULL,dsrtomo_oper,sf_repeat_lop,p,dw,dt,cgiter);
	} else {
	    sf_solver_reg(dsrtomo_oper,sf_cgstep,sf_igrad2_lop,2*nw,nw,nr,dw,dt,cgiter,eps,"verb",verb,"end");
	    sf_cgstep_close();
	}
	
	/* output gradient */
	if (grad != NULL) {
	    if (velocity) {
		for (iw=0; iw < nw; iw++) {
		    dv[iw] = -dw[iw]/(2.*sqrtf(w[iw])*(w[iw]+dw[iw]/2.));
		}
		sf_floatwrite(dv,nw,grad);
	    } else {
		sf_floatwrite(dw,nw,grad);
	    }
	}
	
	/* line search */
	gama = 0.5;
	for (count=0; count < 5; count++) {
	    
	    /* update slowness */
	    for (iw=0; iw < nw; iw++) 
		w1[iw] = (w[iw]+gama*dw[iw])*(w[iw]+gama*dw[iw])/w[iw];
	    
	    /* compute new misfit */
	    dsreiko_fastmarch(t,w1,ff,order);
	    dsreiko_mirror(t);
	    
	    for (ir=0; ir < nr; ir++) {
		if (dp == NULL || dp[ir] == 1) {
		    dt[ir] = t0[ir]-t[(long) ir*n[0]];
		} else {
		    dt[ir] = 0.;
		}
	    }

	    rhsnorm = cblas_snrm2(nr,dt,1);
	    rate = rhsnorm/rhsnorm1;
	    
	    if (rate < 1.) {
		for (iw=0; iw < nw; iw++) w[iw] = w1[iw];
		
		rhsnorm1 = rhsnorm;
		rate = rhsnorm1/rhsnorm0;
		break;
	    }
	    
	    gama *= 0.5;
	}
	
	if (count == 5) {
	    sf_warning("Line-search failure at iteration %d of %d.",iter+1,niter);
	    break;
	}
	
	sf_warning("L2 misfit after iteration %d of %d: %g (line-search %d)",iter+1,niter,rate,count);
    }
    
    /* convert to velocity */
    if (velocity) {
	for (iw=0; iw < nw; iw++) {
	    w[iw] = 1./sqrtf(w[iw]);
	}
    }
    
    sf_floatwrite(w,nw,out);
       
    exit(0);
}
