/* The Gamma Lasso -- Matt Taddy 2013 */

#include <stdlib.h>
#include <string.h>
#include <Rmath.h>
#include <time.h>

#include "vec.h"
#include "lhd.h"
#include "gui.h"

/**** global variables ****/

// argument variables
unsigned int dirty = 0;
int n, p, N;
double nd, pd;

// pointers to arguments
double *Y = NULL;
double *xv = NULL;
int *xi = NULL;
int *xp = NULL;
double *W = NULL;
double *V = NULL;
double *gam = NULL;

int prexx;
double *xbar = NULL;
double *vxsum = NULL;
double *vxz = NULL;
double *vxx = NULL;

// variables to be created
double *omega = NULL;
unsigned int fam;
double A;
double *B = NULL;
double *E = NULL;
double *Z = NULL;
double vsum;

unsigned int npass,nrw;
double l1pen;

double ysum,ybar;
double *H = NULL;
double *G = NULL;
double *ag0 = NULL;

// function pointers
double (*nllhd)(int, double, double*, double*, double*) = NULL;
double (*reweight)(int, double, double*, 
                double *, double*, double*, int*) = NULL;

/* global cleanup function */
void gamlr_cleanup(){
  if(!dirty) return;

  if(B){ free(B); B = NULL; }
  if(G){ free(G); G = NULL; }
  if(H){ free(H); H = NULL; }
  if(ag0){ free(ag0); ag0 = NULL; }

  if(omega){ free(omega); omega = NULL; }
  if(Z){ free(Z); Z = NULL; }

  dirty = 0;
}


// calculates degrees of freedom, as well as
// other gradient dependent statistics.
double dof(int s, double *lam, double L){
  int j;
  
  //calculate absolute grads
  for(j=0; j<p; j++)
    if(isfinite(W[j]) & (B[j]==0.0))
      ag0[j] = fabs(G[j])/W[j];

  // initialization  
  if(s==0){
    if(!isfinite(lam[0]))  
      lam[0] = dmax(ag0,p)/nd;
  }

  double df = 1.0;

  // penalized bit
  double shape,phi;
  if(fam==1) phi = L*2/nd; 
  else phi = 1.0;
  for(j=0; j<p; j++)
    if(isfinite(W[j])){
      if( (gam[j]==0.0) | (W[j]==0.0) ){  
        if( (B[j]!=0.0) ) df ++;
      } else{ // gamma lasso
        shape = lam[s]*nd/gam[j];
        df += pgamma(ag0[j], shape/phi, phi*gam[j], 1, 0); 
      }
    }

  return df;
}

/* The gradient descent move for given direction */
double Bmove(int j)
{
  double dbet;
  if(H[j]==0) return -B[j];

  // unpenalized
  if(W[j]==0.0) dbet = -G[j]/H[j]; 
  else{
    // penalty is lam[s]*nd*W[j]*omega[j]*fabs(B[j])
    double pen, ghb;
    pen = l1pen*W[j]*omega[j];
    ghb = (G[j] - H[j]*B[j]);
    if(fabs(ghb) < pen) dbet = -B[j];
    else dbet = -(G[j]-sign(ghb)*pen)/H[j];
  }
  return dbet;
}

void doxbar(void){
  for(int j=0; j<p; j++){
      xbar[j] = 0.0;
      for(int i=xp[j]; i<xp[j+1]; i++) 
        xbar[j] += xv[i];
      xbar[j] *= 1.0/nd; }
}

void docurve(void){
  for(int j=0; j<p; j++){
    vxsum[j] = vxz[j] = 0.0;
    for(int i=xp[j]; i<xp[j+1]; i++){
      vxsum[j] += V[xi[i]]*xv[i];
      vxz[j] += V[xi[i]]*xv[i]*Z[xi[i]];
    }
    H[j] = curve(xp[j+1]-xp[j], 
      &xv[xp[j]], &xi[xp[j]], xbar[j],
      V, vsum, vxsum[j]);
  }
}

void dograd(int j){
  int k;
  if(prexx){
    G[j] = -vxz[j] + A*vxsum[j]; 
    int jind = j*(j+1)/2;
    for(k=0; k<j; k++)
      G[j] += vxx[jind+k]*B[k];
    for(k=j; k<p; k++)
      G[j] += vxx[k*(k+1)/2 + j]*B[k];
  } 
  else{  
    G[j] = grad(xp[j+1]-xp[j], 
      &xv[xp[j]], &xi[xp[j]], 
      vxsum[j], vxz[j],
      A, E, V); 
  }
}

/* coordinate descent for log penalized regression */
int cdsolve(double tol, int M, int RW)
{
  int rw,t,i,j,dozero,dopen,exitstat; 
  double dbet,imove,Bdiff;

  // initialize
  dopen = isfinite(l1pen);
  Bdiff = INFINITY;
  exitstat = 0;
  dozero = 1;
  t = 0;
  rw = 0;

  // CD loop
  while( (Bdiff > tol) | dozero ){

    Bdiff = 0.0;
    imove = 0.0;
    if(dozero)
      if(fam!=1){
          rw +=1;
          vsum = reweight(n, A, E, Y, V, Z, &exitstat);
          docurve();
          dbet = intercept(n, E, V, Z, vsum)-A;
          A += dbet;
          Bdiff = fabs(vsum*dbet*dbet);
      }

    /****** cycle through coefficients ******/
    for(j=0; j<p; j++){

      // always skip the zero sd var
      if(!isfinite(W[j])) continue;

      // skip the in-active set unless 'dozero'
      if(!dozero & (B[j]==0.0) & (W[j]>0.0)) continue;

      // update gradient
      dograd(j);

      // for null model skip penalized variables
      if(!dopen & (W[j]>0.0)){ dbet = 0.0; continue; }

      // calculate the move and update
      dbet = Bmove(j);
      if(dbet!=0.0){ 
        B[j] += dbet;
        if(!prexx)
          for(i=xp[j]; i<xp[j+1]; i++)
            E[xi[i]] += xv[i]*dbet; 
        A += -vxsum[j]*dbet/vsum;
        Bdiff = fmax(Bdiff,H[j]*dbet*dbet);
      }
    }

    // break for intercept only linear model
    if( (fam==1) & (Bdiff==0.0) & dozero ) break;

    // iterate
    t++;

    // check for max iterations
    if(t == M){
      shout("Warning: hit max CD iterations.  "); 
      exitstat = 2;
      break;
    }

    if(rw == RW){
      // just break; if you hit this we assume you're limiting
      // re-weights as an intentional approximation.
      break;
    }


    // check for active set update
    if(dozero == 1) dozero = 0;
    else if(Bdiff < tol) dozero = 1; 

  }

  // calc preds if they were skipped
  if(prexx & (N>0)){
    // got to figure out what to do with this if N=0
    zero_dvec(E,n);
    for(j=0; j<p; j++)
      if(B[j]!=0)
        for(i=xp[j];i<xp[j+1];i++)
          E[xi[i]] += xv[i]*B[j];
  }

  npass = t;
  nrw = rw; 
  return exitstat;
}

/*
 * Main Function: gamlr
 * path estimation of adaptively penalized coefficients
 *
 */

 void gamlr(int *famid, // 1 gaus, 2 bin, 3 pois
            int *n_in, // nobs 
            int *p_in, // nvar
            int *N_in, // length of nonzero x entries
            int *xi_in, // length-l row ids for nonzero x
            int *xp_in, // length-p+1 pointers to each column start
            double *xv_in, // nonzero x entry values
            double *y_in, // length-n y
            int *prexx_in, // indicator for using covariance updates
            double *xbar_in,  // un-weighted covariate means
            double *vxsum_in, // weighted sums of x values
            double *vxx_in, // dense columns of upper tri for XVX
            double *vxy_in, // weighted correlation between x and y
            double *eta, // length-n fixed shifts (assumed zero for gaussian)
            double *varweight, // length-p weights
            double *obsweight, // length-n weights
            int *standardize, // whether to scale penalty by sd(x_j)
            int *nlam, // length of the path
            double *delta, // path stepsize
            double *gamvec,  // gamma in the GL paper
            double *thresh,  // cd convergence
            int *maxit, // cd max iterations 
            int *maxrw, // max irls reweights
            double *lambda, // output lambda
            double *deviance, // output deviance
            double *df, // output df
            double *alpha,  // output intercepts
            double *beta, // output coefficients
            int *exits, // exit status.  0 is normal, 1 warn, 2 break path
            int *verb) // talk? 
 {
  dirty = 1; // flag to say the function has been called
  // time stamp for periodic R interaction
  time_t itime = time(NULL);  

  /** Build global variables **/
  fam = *famid;
  n = *n_in;
  p = *p_in;
  nd = (double) n;
  pd = (double) p;
  N = *N_in;

  E = eta;
  Y = y_in;
  ysum = sum_dvec(Y,n); 
  ybar = ysum/nd;

  xi = xi_in;
  xp = xp_in;
  xv = xv_in;
  prexx = prexx_in[0];
  xbar = xbar_in;
  vxsum = vxsum_in;
  vxx = vxx_in;
  vxz = vxy_in;

  H = new_dvec(p);
  W = varweight;
  omega = drep(1.0,p);  // gamma lasso adaptations
  Z = new_dup_dvec(Y,n);
  V = obsweight;
  vsum = sum_dvec(V,n);    

  if(prexx){
    for(int j=0; j<p; j++)
      H[j] = vxx[j*(j+1)/2+j] 
        + xbar[j]*(xbar[j]*vsum - 2.0*vxsum[j]); 
  } 
  else{
    doxbar();
    if(*standardize | (fam==1)) docurve(); 
  }

  if(*standardize){
    for(int j=0; j<p; j++){
      if(fabs(H[j])<1e-10){ H[j]=0.0; W[j] = INFINITY; }
      else W[j] *= sqrt(H[j]/vsum);
    }
  }

  A=0.0;
  B = new_dzero(p);
  G = new_dzero(p);
  ag0 = new_dzero(p);
  gam = gamvec;

  // some local variables
  double Lold, NLLHD, NLsat;
  int s;

  // family dependent settings
  switch( fam )
  {
    case 2:
      nllhd = &bin_nllhd;
      reweight = &bin_reweight;
      A = log(ybar/(1-ybar));
      NLsat = 0.0;
      break;
    case 3:
      nllhd = &po_nllhd;
      reweight = &po_reweight;
      A = log(ybar);
      // nonzero saturated negative log likelihood
      NLsat = ysum;
      for(int i=0; i<n; i++)
        if(Y[i]!=0) NLsat += -Y[i]*log(Y[i]);
      break;
    default: 
      fam = 1; // if it wasn't already
      nllhd = &sse;
      NLsat=0.0;
      A = intercept(n, E, V, Z, vsum);
      for(int j=0; j<p; j++) dograd(j);
  }

  l1pen = INFINITY;
  Lold = INFINITY;
  NLLHD =  nllhd(n, A, E, Y, V);

  if(*verb)
    speak("*** n=%d observations and p=%d covariates ***\n", n,p);

  // move along the path
  for(s=0; s<*nlam; s++){

    // deflate the penalty
    if(s>0)
      lambda[s] = lambda[s-1]*(*delta);
    l1pen = lambda[s]*nd;

    // run descent
    exits[s] = cdsolve(*thresh,maxit[s],maxrw[s]);

    // update parameters and objective
    maxit[s] = npass;
    maxrw[s] = nrw;
    Lold = NLLHD;
    if( (N>0) | (s==0) ) 
      NLLHD =  nllhd(n, A, E, Y, V);
    deviance[s] = 2.0*(NLLHD - NLsat);
    df[s] = dof(s, lambda, NLLHD);
    alpha[s] = A;
    copy_dvec(&beta[s*p],B,p);

    if(s==0) *thresh *= deviance[0]; // relativism
    
    // gamma lasso updating
    for(int j=0; j<p; j++) 
      if(gam[j]>0.0){
        if(isfinite(gam[j])){
          if( (W[j]>0.0) & isfinite(W[j]) )
            omega[j] = 1.0/(1.0+gam[j]*fabs(B[j])); } 
        else if(B[j]!=0.0) omega[j] = 0.0; 
      }
      
    // verbalize
    if(*verb) 
      speak("segment %d: lambda = %.4g, dev = %.4g, npass = %d\n", 
          s+1, lambda[s], deviance[s], npass);

    // exit checks
    if(deviance[s]<0.0){
      exits[s] = 2;
      shout("Warning: negative deviance.  ");
    }
    if(df[s] >= nd){
      exits[s] = 2;
      shout("Warning: saturated model.  "); 
    }
    if(exits[s]==2){
      shout("Finishing path early.\n");
      *nlam = s; break; }

    itime = interact(itime); 
  }

  // deviance calcs are wrong for null X
  // so we just make the last model look best
  if( (N==0) & (s>0) ) deviance[*nlam-1] = 0.0;
  gamlr_cleanup();
}










