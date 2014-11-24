#include "../Blitz.h"
#include "OptimizedBreakup.h"
#include "../Splines/Grid.h"
#include "../Splines/CubicSpline.h"
#include <vector>
#include <algorithm>
#include "../Integration/GKIntegration.h"
#include <complex>

const int DIM = 3;

class UshortIntegrand
{
private:
  CubicSpline &Vl, &V;
  inline double Vintegrand(double r) { return r*r*(V(r) - Vl(r)); }
public:
  inline double operator()(double r) { return Vintegrand(r); }
  UshortIntegrand (CubicSpline &t_Vl, CubicSpline &t_V) :
    Vl(t_Vl), V(t_V)
  {}
};

class UlongIntegrand
{
private:
  CubicSpline &Vl, &V;
  inline double Vintegrand(double r) { return r*r*(Vl(r)); }
public:
  inline double operator()(double r) { return Vintegrand(r); }
  UlongIntegrand (CubicSpline &t_Vl, CubicSpline &t_V) :
    Vl(t_Vl), V(t_V)
  {}
};

bool same(pair<double,double> &a, pair<double,double> &b)
{
  return fabs(a.first-b.first)<1e-10;
}


bool pairCompare(pair<double,double> &a, pair<double,double> &b)
{
  return a.first == b.first;
}

class XkIntegrand
{
private:
  CubicSpline &V;
  double k;
  inline double Vintegrand(double r) { return r*sin(k*r)*V(r); }
public:
  inline double operator()(double r) { return Vintegrand(r); }
  XkIntegrand (CubicSpline &t_V, double t_k) :
    V(t_V), k(t_k)
  {}
};

class EwaldClass
{
public:
  dVec box;
  double Z1Z2, vol, rMin, rCut, tau;
  Array<dVec,1> kVecs;
  Array<double,1> MagK;
  double kCut;
  int nPoints, nKnots;
  int gridType, breakupType, breakupObject, paIndex;
  Grid &grid;

  EwaldClass(double t_Z1Z2, dVec t_Box, double t_kCut, double t_rMin, double t_rCut, int t_nPoints,
             Grid &t_grid, int t_breakupType, int t_breakupObject, int t_paIndex, int t_nKnots, double t_tau)
    : Z1Z2(t_Z1Z2), box(t_Box), kCut(t_kCut), rMin(t_rMin), rCut(t_rCut), nPoints(t_nPoints),
      grid(t_grid), breakupType(t_breakupType), breakupObject(t_breakupObject), paIndex(t_paIndex), nKnots(t_nKnots), tau(t_tau)
  {
    if (rMin == 0.)
      cerr << "Warning: rMin = 0!" << endl;
    vol = 1.;
    for (int i=0; i<DIM; i++)
      vol *= box[i];
    SetupkVecs();
  }

  double DoBreakup()
  {
    if (breakupType == 0)
      BasicEwald(sqrt((kCut/2.)/rCut)); // HACK: Hard-coded alpha
    else if (breakupType == 1)
      OptimizedBreakup();
    else
      cerr << "ERROR: Unrecognized breakup type." << endl;
  }


  double Xk_Coul(double k, double r)
  {
    double Xk = -(4.0*M_PI*Z1Z2/(k*k))*cos(k*r);
    if (breakupObject == 1)
      Xk *= tau;
    return Xk;
  }

  /// This calculates the quantity 
  /// \f$ X_k \equiv -\frac{4 \pi}{\Omega k} \int_{r_c}^\infty dr r \sin(kr) V(r).\f$
  double CalcXk(CubicSpline &V, double rc, double k, double rMax)
  {
     double Xk = 0.;
     double absTol = 1.0e-11;
     double relTol = 1.0e-11;

     XkIntegrand integrand(V, k);
     GKIntegration<XkIntegrand, GK31> integrator(integrand);

     double rEnd;
     double rFirst = rc + ((M_PI/k)-(remainder(rc,(M_PI/k))));
     if (rMax >= rFirst) {
       // First segment
       Xk += -(4.0*M_PI/k) * integrator.Integrate(rc, rFirst, absTol, relTol, false);

       // Subsequent segments
       //double nPik = int(k)*M_PI/k; // HACK: This may need to be adjusted.
       double nPik = int(k)*M_PI/k; // HACK: This may need to be adjusted.
       int nSeg = floor((rMax-rFirst)/nPik);
       for (int i=0; i<nSeg; i++)
         Xk += -(4.0*M_PI/k) * integrator.Integrate(rFirst + i*nPik, rFirst + (i+1)*nPik, absTol, relTol, false);
       rEnd = rFirst + (nSeg)*nPik;
     } else if (rMax >= rc) {
       Xk += -(4.0*M_PI/k) * integrator.Integrate(rc, rMax, absTol, relTol, false);
       rEnd = rMax;
     } else
       rEnd = rc;

     /// Add in the analytic part that I ignored
     /// Multiply analytic term by tau only for U -- do not multiply
     /// for dU or V.
     Xk += Xk_Coul(k, rEnd);

     return Xk;
  }

  inline bool Include(dVec k, bool includeAll=false)
  {
    if (includeAll)
      return true;
    //  assert (DIM == 3);
    double MagK = sqrt(dot(k,k));
    if (MagK <= kCut) {
      if (abs(k[0]) > 0.0)
        return true;
      else if ((k[0]==0.0) && (abs(k[1]) > 0.0))
        return true;
      else if ((DIM==3) && ((k[0]==0.0) && (k[1]==0.0) && (abs(k[2]) > 0.0)))
        return true;
      else
        return false;
    } else
      return false;
  }

  void SetupkVecs(bool includeAll=false)
  {
    cout << "Setting up k vectors..." << endl;
    dVec kBox;
    for (int i=0; i<DIM; i++)
      kBox[i] = 2.0*M_PI/box[i];

    int numVecs = 0;
    dVec MaxkIndex;
    for (int i=0;i<DIM;i++)
      MaxkIndex[i] = (int)ceil(1.1*kCut/kBox[i]);

    dVec k;
    TinyVector<int,DIM> ki;
    for (int ix=-MaxkIndex[0]; ix<=MaxkIndex[0]; ix++) {
      k[0] = ix*kBox[0];
      if (DIM > 1) {
        for (int iy=-MaxkIndex[1]; iy<=MaxkIndex[1]; iy++) {
          k[1] = iy*kBox[1];
          if (DIM > 2) {
            for (int iz=-MaxkIndex[2]; iz<=MaxkIndex[2]; iz++) {
              k[2] = iz*kBox[2];
              if (Include(k,includeAll))
                numVecs++;
            }
          } else {
            if (Include(k,includeAll))
              numVecs++;
          }
        }
      } else {
        if (Include(k,includeAll))
           numVecs++;
      }
    }
    //  kIndices.resize(numVecs);
    kVecs.resize(numVecs);
    MagK.resize(numVecs);

    //   for (int i=0; i<DIM; i++)
    //     C[i].resize(2*MaxkIndex[i]+1);
    numVecs = 0;
    for (int ix=-MaxkIndex[0]; ix<=MaxkIndex[0]; ix++) {
      k[0] = ix*kBox[0];
      ki[0]= ix+MaxkIndex[0];
      if (DIM > 1) {
        for (int iy=-MaxkIndex[1]; iy<=MaxkIndex[1]; iy++) {
          k[1] = iy*kBox[1];
          ki[1]= iy+MaxkIndex[1];
          if (DIM > 2) {
            for (int iz=-MaxkIndex[2]; iz<=MaxkIndex[2]; iz++) {
              k[2] = iz*kBox[2];
              ki[2] = iz+MaxkIndex[2];
              if (Include(k,includeAll)) {
                kVecs(numVecs) = k;
                MagK(numVecs) = sqrt(dot(k,k));
                numVecs++;
              }
            }
          } else {
            if (Include(k,includeAll)) {
              kVecs(numVecs) = k;
              MagK(numVecs) = sqrt(dot(k,k));
              numVecs++;
            }
          }
        }
      } else {
        if (Include(k,includeAll)) {
          kVecs(numVecs) = k;
          MagK(numVecs) = sqrt(dot(k,k));
          numVecs++;
        }
      }
    }

    cout << "kCut = " << kCut << ", maxKIndex(0) = " << MaxkIndex[0] << ", nK = " << numVecs << endl;
  }

  double OptimizedBreakup()
  {
    const double tolerance = 1.0e-12;
    double boxVol = box[0]*box[1]*box[2];
    double kvol = 1.0; // Path.GetkBox()[0];
    for (int i=0; i<DIM; i++)
      kvol *= 2*M_PI/box[i]; // Path.GetkBox()[i];
    double kavg = pow(kvol,1.0/DIM);

    // Read potential
    cout << "Reading files..." << endl;
    string objectString;
    if (breakupObject == 2)
      objectString = "dud";
    else if (breakupObject == 1)
      objectString = "ud";
    else if (breakupObject == 0)
      objectString = "v";
    else {
      cerr << "ERROR: Unrecognized breakup object type!" << endl;
      abort();
    }
    stringstream fileName;
    fileName << objectString << "." << paIndex << ".txt";
    ifstream pointFile;
    string fileNameStr = fileName.str();
    pointFile.open(fileNameStr.c_str());
    Array<double,1> rs, tmpV;
    int ri = 0;
    while (!pointFile.eof()) {
      double r, V;
      pointFile>>r;
      if (!pointFile.eof()) {
        rs.resizeAndPreserve(ri+1);
        tmpV.resizeAndPreserve(ri+1);
        pointFile>>V;
        rs(ri) = r;
        tmpV(ri) = V;
        //if (breakupObject == 2)
        //  tmpV(ri) *= tau;
        ri += 1;
      }
    }
    pointFile.close();
    GeneralGrid tmpVGrid;
    tmpVGrid.Init(rs);
    CubicSpline VSpline(&tmpVGrid,tmpV);

    // Set up basis
    LPQHI_BasisClass basis;
    basis.SetBox(box);
    basis.SetNumKnots(nKnots);
    basis.Set_rc(rCut);

    // We try to pick kcont to keep reasonable number of k-vectors.
    // It turns out you need way more kVecs for the Optimized breakup
    // procedure when fitting the parameters. In simulation, you can 
    // then use much fewer. This is why we define:
    // kc - largest kVec used in simluation
    // kCont - largest exactly calculated kVec in breakup fitting
    // kMax - largest approximately included kVec in breakup fitting
    //
    double kCont = 50.0 * kavg;
    double delta = basis.GetDelta();
    double kMax = 50.0*M_PI/delta;
    assert(kMax > kCont);
    cout << "kAvg = " << kavg << ", kCut = " << kCut << ", kCont = " << kCont << ", kMax = " << kMax << endl;

    OptimizedBreakupClass breakup(basis);
    cout << "Setting up breakup kVecs..." << endl;
    breakup.SetkVecs (kCut, kCont, kMax);
    int numk = breakup.kpoints.size();
    int N = basis.NumElements();
    Array<bool,1> adjust(N);
    Array<double,1> t(N), Xk(numk), fVl(kVecs.size()), Vl(nPoints), Vs(nPoints);
    fVl = 0.0;
    Vl = 0.0;

    // Determine rMax from tolerances
    double VTol = 1.e-7;
    double rMax = VSpline.grid->End;
    double Vc = Z1Z2/rMax;
    if (breakupObject == 1)
      Vc *= tau;
    if (abs(VSpline(rMax) - Vc) > VTol)
      cout << "WARNING: |V(rMax) - V_{c}(rMax)| = " << abs(VSpline(rMax) - Vc) << " > " << VTol << " with rMax = " << rMax << ", V(rMax) = " << VSpline(rMax) << ", V_{c}(rMax) = " << Vc << endl;
    else {
      double r;
      int i = 0;
      Array<double,1> tmpPoints = VSpline.grid->Points();
      do {
        r = tmpPoints(i);
        Vc = Z1Z2/r;
        if (breakupObject == 1)
          Vc *= tau;
        rMax = tmpPoints(i+1);
        i += 1;
      }
      while (abs(VSpline(r) - Vc) > VTol);
    }
    if (rMax < rCut)
      rMax = rCut;
    //rMax = rCut;
    Vc = Z1Z2/rMax;
    if (breakupObject == 1)
      Vc *= tau;
    cout << "Setting rMax = " << rMax << " with |V(rMax) - V_{c}(rMax)| < " << VTol << ", V(rMax) = " << VSpline(rMax) << ", V_{c}(rMax) = " << Vc << endl;

    // Calculate Xk's
    cout << "Calculating Xk's..." << endl;
    Array<double,1> tmpXk(Xk.size());
    for (int ki=0; ki<numk; ki++) {
      double k = breakup.kpoints(ki)[0];
      Xk(ki) = CalcXk(VSpline, rCut, k, rMax) / boxVol;
      //cout << k << " " << Xk(ki) << endl;
      //abort();
      //Xk(ki) = Xk_Coul(k, rCut) / boxVol;
      if (ki % int(numk/10) == 0)
        cout << ceil(100*double(ki)/double(numk)) << " % complete" << endl;
    }

    // Set boundary conditions at rCut:  For Maxe value and first and
    // second derivatives of long-range potential to match the full
    // potential at rCut.
    adjust = true;
    delta = basis.GetDelta();
    //     t(N-3) = pa.V(rCut);                 adjust(N-3) = false;
    //     t(N-2) = pa.Vp(rCut)*delta;          adjust(N-2) = false;
    //     t(N-1) = pa.Vpp(rCut)*delta*delta;   adjust(N-1) = false;
    //     t(1) = 0.0;                        adjust(1)   = false;

    //     t(N-3) = 1.0/delta;                 adjust(N-3) = false;
    //     t(N-2) = (-1.0/(delta*delta))*delta;          adjust(N-2) = false;
    //     t(N-1) = (2.0/(delta*delta*delta))*delta*delta;   adjust(N-1) = false;
    //     t(1) = 0.0;                        adjust(1)   = false;


    // Now, do the optimal breakup:  this gives me the coefficents
    // of the basis functions, h_n in the array t.
    cout << "Performing breakup..." << endl;
    double chi = breakup.DoBreakup (Xk, t, adjust);
    cerr<<"Chi = "<<chi<<endl;

    // Now, we must put this information into the pair action
    // object.  First do real space part
    double Vl0 = 0.0;
    for (int n=0; n<N; n++)
      Vl0 += t(n)*basis.h(n,0.0);

    cout << grid.NumPoints << " " << grid.Start << " " << grid.End << endl;
    for (int i=0; i<grid.NumPoints; i++) {
      double r = grid(i);
      if (r <= rCut) {
        // Sum over basis functions
        for (int n=0; n<N; n++)
          Vl(i) += t(n) * basis.h(n, r);
      }
      else {
        //cout << "WARNING: Why is r bigger than rCut?" << endl;
        Vl(i) = VSpline(r); // 0.0; //hack!  pa.V (r);
      }
    }

    // Calculate FT of Ushort at k=0
    CubicSpline VlSpline(&grid,Vl);
    UshortIntegrand shortIntegrand(VlSpline,VSpline);
    GKIntegration<UshortIntegrand, GK31> shortIntegrator(shortIntegrand);
    shortIntegrator.SetRelativeErrorMode();
    double fVs0 = 4.0*M_PI/boxVol * shortIntegrator.Integrate(1.0e-100, rCut, tolerance);

    // Calculate FT of Vl at k=0
    UlongIntegrand longIntegrand(VlSpline,VSpline);
    GKIntegration<UlongIntegrand, GK31> longIntegrator(longIntegrand);
    longIntegrator.SetRelativeErrorMode();
    double fVl0 = 4.0*M_PI/boxVol * longIntegrator.Integrate(1.0e-100, rCut, tolerance);

    // Print out values
    cout << "Vl0 = " << Vl0 << ", fVs0 = " << fVs0 << ", fVl0 = " << fVl0 << endl;

    // Write potential
    stringstream rFileName;
    rFileName << objectString << "." << paIndex << ".r.txt";
    string rFileNameStr = rFileName.str();
    ofstream outfile;
    outfile.setf(ios::scientific);
    outfile.precision(10);
    outfile.open(rFileNameStr.c_str());
    outfile<<0.<<" "<<Vl0<<endl;
    for (int i=0;i<grid.NumPoints; i++){
      double r=grid(i);
      outfile<<r<<" "<<Vl(i)<<endl;
    }
    outfile.close();

    // Now do k-space part
    stringstream kFileName;
    kFileName << objectString << "." << paIndex << ".k.txt";
    string kFileNameStr = kFileName.str();
    outfile.open(kFileNameStr.c_str());
    outfile<<0.0<<" "<<-fVs0<<endl; // fixme: check this
    vector<pair<double, double> > fVls;
    for (int ki=0; ki < kVecs.size(); ki++) {
      double k = sqrt(dot(kVecs(ki),kVecs(ki)));
      // Sum over basis functions
      for (int n=0; n<N; n++)
        fVl(ki) += t(n) * basis.c(n,k);
      // Now add on part from rCut to infinity
      //fVl(ki) -= Xk(ki);
      fVl(ki) -= CalcXk(VSpline, rCut, k, rMax) / boxVol;
      //fVl(ki) -= Xk_Coul(k, rCut) / boxVol;
      fVls.push_back(make_pair(k, fVl(ki)));
    }

    // delete all elements past new_end
    sort(fVls.begin(), fVls.end());
    fVls.erase( unique(fVls.begin(), fVls.end(), pairCompare), fVls.end());

    // Write values
    for (int i=0;i<fVls.size();i++)
      outfile<<fVls[i].first<<" "<<fVls[i].second<<endl; // *vol<<endl;
    outfile.close();

  }

  void BasicEwald(double alpha)
  {
    cout << "alpha = " << alpha << endl;

    // Read potential
    cout << "Reading files..." << endl;
    string objectString;
    if (breakupObject == 2)
      objectString = "dud";
    else if (breakupObject == 1)
      objectString = "ud";
    else if (breakupObject == 0)
      objectString = "v";
    else {
      cerr << "ERROR: Unrecognized breakup object type!" << endl;
      abort();
    }
    stringstream fileName;
    fileName << objectString << "." << paIndex << ".txt";
    ifstream pointFile;
    string fileNameStr = fileName.str();
    pointFile.open(fileNameStr.c_str());
    Array<double,1> rs, tmpV;
    int ri = 0;
    while (!pointFile.eof()) {
      double r, V;
      pointFile>>r;
      if (!pointFile.eof()) {
        rs.resizeAndPreserve(ri+1);
        tmpV.resizeAndPreserve(ri+1);
        pointFile>>V;
        rs(ri) = r;
        tmpV(ri) = V;
        //if (breakupObject == 2)
        //  tmpV(ri) *= tau;
        ri += 1;
      }
    }
    pointFile.close();
    GeneralGrid tmpVGrid;
    tmpVGrid.Init(rs);
    CubicSpline VSpline(&tmpVGrid,tmpV);

    // Short-ranged r-space part
    stringstream rFileName;
    rFileName << objectString << "." << paIndex << ".r.txt";
    string rFileNameStr = rFileName.str();
    ofstream outfile;
    outfile.setf(ios::scientific);
    outfile.precision(10);
    outfile.open(rFileNameStr.c_str());
    double Vl0 = Z1Z2*2.*alpha/sqrt(M_PI);
    if (breakupObject == 1)
      Vl0 *= tau;
    outfile<<0.<<" "<<Vl0<<endl;
    Array<double,1> Vls(nPoints);
    for (int i=0; i<grid.NumPoints; i++){
      double r = grid(i);
      if (breakupObject == 1)
        Vls(i) = Z1Z2*tau*erf(alpha*r)/r;
      else
        Vls(i) = Z1Z2*erf(alpha*r)/r;
      outfile<<r<<" "<<Vls(i)<<endl;
    }
    outfile.close();

    if (breakupObject == 1)
      cout << "V(rCut) - Vs(rCut) = " << VSpline(rCut) - Z1Z2*tau*erf(alpha*rCut)/rCut << endl;
    else
      cout << "V(rCut) - Vs(rCut) = " << VSpline(rCut) - Z1Z2*erf(alpha*rCut)/rCut << endl;

    // Long-ranged k-space part
    stringstream kFileName;
    kFileName << objectString << "." << paIndex << ".k.txt";
    string kFileNameStr = kFileName.str();
    outfile.open(kFileNameStr.c_str());
    double fVs0 = -4.0*M_PI*Z1Z2/(4.0*alpha*alpha*vol);
    if (breakupObject == 1)
      fVs0 *= tau;
    outfile<<0.0<<" "<<fVs0<<endl;
    vector<pair<double, double> > fVls;
    double kConst = 0.;
    for (int i=0; i<kVecs.size(); ++i) {
      dVec k = kVecs(i);
      double k2 = dot(k,k);
      double kMag = sqrt(k2);
      double fVl = (4.*M_PI*Z1Z2/(k2*vol)) * exp(-k2/(4.*alpha*alpha));
      kConst += fVl;
      if (breakupObject == 1)
        fVl *= tau;
      fVls.push_back(make_pair(kMag, fVl));
    }
    cout << "kConst = " << kConst << endl;

    // delete all elements past new_end
    sort(fVls.begin(), fVls.end());
    fVls.erase( unique(fVls.begin(), fVls.end(), pairCompare), fVls.end());

    // Write values
    for (int i=0;i<fVls.size();i++)
      outfile<<fVls[i].first<<" "<<fVls[i].second<<endl; // *vol<<endl;
    outfile.close();

    // Print out values
    cout << "Vl0 = " << Vl0 << ", fVs0 = " << fVs0 << endl;

  }

  void ComputeMadelungNaive(int nImages)
  {
    cout << "Computing Madelung constant by naive sum with " << nImages << " images..." << endl;

    // Get Long-ranged k-space part
    string objectString;
    if (breakupObject == 2)
      objectString = "dud";
    else if (breakupObject == 1)
      objectString = "ud";
    else if (breakupObject == 0)
      objectString = "v";
    else {
      cerr << "ERROR: Unrecognized breakup object type!" << endl;
      abort();
    }
    // Get r-space part
    stringstream rFileName;
    rFileName << objectString << "." << paIndex << ".txt";
    string rFileNameStr = rFileName.str();
    ifstream pointFile;
    pointFile.open(rFileNameStr.c_str());
    Array<double,1> rs, tmpV;
    int ri = 0;
    while (!pointFile.eof()) {
      double r, V;
      pointFile>>r;
      if (!pointFile.eof()) {
        rs.resizeAndPreserve(ri+1);
        tmpV.resizeAndPreserve(ri+1);
        pointFile>>V;
        rs(ri) = r;
        tmpV(ri) = V/Z1Z2;
        //if (breakupObject == 2)
        //  tmpV(ri) *= tau;
        ri += 1;
      }
    }
    pointFile.close();
    GeneralGrid tmpVGrid;
    tmpVGrid.Init(rs);
    CubicSpline VSpline(&tmpVGrid,tmpV);

    // Set Coordinates
    double s1 = box[0]/2.;
    int N = 8;
    Array<dVec,1> xs(N);
    Array<double,1> Qs(N);
    if (DIM==3) {
      dVec x0(0,0,0);
      xs(0) = x0;
      dVec x1(s1,s1,0);
      xs(1) = x1;
      dVec x2(s1, 0, s1);
      xs(2) = x2;
      dVec x3(0, s1, s1);
      xs(3) = x3;
      dVec x4(s1, 0, 0);
      xs(4) = x4;
      dVec x5(0, s1, 0);
      xs(5) = x5;
      dVec x6(0, 0, s1);
      xs(6) = x6;
      dVec x7(s1, s1, s1);
      xs(7) = x7;
      Qs(0) = 1.0;
      Qs(1) = 1.0;
      Qs(2) = 1.0;
      Qs(3) = 1.0;
      Qs(4) = -1.0;
      Qs(5) = -1.0;
      Qs(6) = -1.0;
      Qs(7) = -1.0;
    }

    // Short-ranged r-space
    double L = box[0];
    double Li = 1.0/L;
    double Vs = 0.;
    for (int i=0; i<xs.size()-1; i++) {
      for (int j=i+1; j<xs.size(); j++) {
        for (int a=-nImages; a<=nImages; a++) {
          for (int b=-nImages; b<=nImages; b++) {
            for (int c=-nImages; c<=nImages; c++) {
              dVec r = xs(i) - xs(j);
              r[0] += a*L;
              r[1] += b*L;
              r[2] += c*L;
              double magr = sqrt(dot(r,r));
              if (magr > tmpVGrid.End) {
                double tmpVs = Qs(i)*Qs(j)/magr;
                if (breakupObject == 1)
                  tmpVs *= tau;
                Vs += tmpVs;
              } else
                Vs += Qs(i)*Qs(j)*VSpline(magr);
            }
          }
        }
      }
    }

    // Self-energy
    double Vself = 0.;
    for (int i=0; i<xs.size(); i++) {
      for (int a=-nImages; a<=nImages; a++) {
        for (int b=-nImages; b<=nImages; b++) {
          for (int c=-nImages; c<=nImages; c++) {
            if ((a == 0) && (b == 0) && (c == 0))
              Vself += 0.;
            else {
              dVec r = xs(i) - xs(i);
              r[0] += a*L;
              r[1] += b*L;
              r[2] += c*L;
              double magr = sqrt(dot(r,r));
              if (magr > tmpVGrid.End) {
                double tmpVself = Qs(i)*Qs(i)/magr;
                if (breakupObject == 1)
                  tmpVself *= tau;
                Vself += tmpVself;
              } else
                Vself += Qs(i)*Qs(i)*VSpline(magr);
            }
          }
        }
      }
    }

    cout << "Vs = " << Vs << ", Vself = " << Vself << endl;
    double V = Vs + 0.5*Vself;
    cout << "V = Vs + Vself/2 = " << V << endl;

    // Madelung Constant
    double estMad = box[0]*V/N;
    printf("Vmad = %.10e \n",estMad);
  }

  void ComputeMadelung()
  {
    cout << "Computing Madelung constant..." << endl;

    // Get Long-ranged k-space part
    string objectString;
    if (breakupObject == 2)
      objectString = "dud";
    else if (breakupObject == 1)
      objectString = "ud";
    else if (breakupObject == 0)
      objectString = "v";
    else {
      cerr << "ERROR: Unrecognized breakup object type!" << endl;
      abort();
    }
    stringstream kFileName;
    kFileName << objectString << "." << paIndex << ".k.txt";
    string kFileNameStr = kFileName.str();
    ifstream infile;
    infile.open(kFileNameStr.c_str());
    vector<double> ks;
    vector<double> fVls;
    double fV0 = 0.;
    while (!infile.eof()) {
      double k;
      double fVl;
      infile>>k;
      if (!infile.eof()) {
        infile>>fVl;
        ks.push_back(k);
        fVls.push_back(fVl/Z1Z2);
        if (k == 0.0)
          fV0 = fVl/Z1Z2;
      }
    }
    infile.close();

    // Get Short-ranged r-space part
    stringstream rFileName;
    rFileName << objectString << "." << paIndex << ".r.txt";
    string rFileNameStr = rFileName.str();
    ifstream pointFile;
    pointFile.open(rFileNameStr.c_str());
    Array<double,1> rs(nPoints);
    Array<double,1> Vss(nPoints);
    int ri = 0;
    double Vl0 = 0.;
    while (!pointFile.eof()) {
      double r;
      double Vl;
      pointFile>>r;
      if (!pointFile.eof()) {
        pointFile>>Vl;
        if (r == 0.) {
          Vl0 = Vl/Z1Z2;
        } else {
          rs(ri) = r;
          double Vc;
          if (breakupObject == 1)
            Vc = tau/r;
          else
            Vc = 1./r;
          Vss(ri) = Vc - Vl/Z1Z2;
          ri += 1;
        }
      }
    }
    pointFile.close();
    CubicSpline VsSpline(&grid,Vss);

    // Set Coordinates
    double s1 = box[0]/2.;
    int N = 8;
    Array<dVec,1> xs(N);
    Array<double,1> Qs(N);
    if (DIM==3) {
      dVec x0(0,0,0);
      xs(0) = x0;
      dVec x1(s1,s1,0);
      xs(1) = x1;
      dVec x2(s1, 0, s1);
      xs(2) = x2;
      dVec x3(0, s1, s1);
      xs(3) = x3;
      dVec x4(s1, 0, 0);
      xs(4) = x4;
      dVec x5(0, s1, 0);
      xs(5) = x5;
      dVec x6(0, 0, s1);
      xs(6) = x6;
      dVec x7(s1, s1, s1);
      xs(7) = x7;
      Qs(0) = 1.0;
      Qs(1) = 1.0;
      Qs(2) = 1.0;
      Qs(3) = 1.0;
      Qs(4) = -1.0;
      Qs(5) = -1.0;
      Qs(6) = -1.0;
      Qs(7) = -1.0;
    }

    // Short-ranged r-space
    double L = box[0];
    double Li = 1.0/L;
    double Vs = 0.;
    for (int i=0; i<xs.size()-1; i++) {
      for (int j=i+1; j<xs.size(); j++) {
        dVec r = xs(i) - xs(j);
        for (int d=0; d<DIM; d++)
          r(d) -= floor(r(d)*Li + 0.5)*L; // Put in box
        double magr = sqrt(dot(r,r));
        if (magr <= rCut)
          Vs += Qs(i)*Qs(j)*VsSpline(magr);
      }
    }

    // Long-ranged k-space
    Array<double,1> fVl(kVecs.size());
    fVl = 0.;
    for (int i=0; i<kVecs.size(); i++) {
      bool foundMe = false;
      for (int j=0; j<ks.size(); j++) {
        if (fabs(MagK(i)-ks[j])<1.e-4) {
          if (!foundMe) {
            foundMe = true;
            fVl(i) = fVls[j];
          }
        }
      }
    }

    double Vl = 0.;
    for (int i=0; i<kVecs.size(); i++) {
      if(MagK(i) < kCut) {
        double Re = 0.;
        double Im = 0.;
        for (int j=0; j<xs.size(); j++) {
          double h = dot(kVecs(i), xs(j));
          Re += Qs(j)*cos(h);
          Im -= Qs(j)*sin(h);
        }
        for (int j=0; j<xs.size(); j++) {
          double h = dot(kVecs(i), xs(j));
          Vl += Qs(j) * (Re*cos(h) - Im*sin(h)) * fVl(i);
        }
      }
    }
    Vl *= 0.5;

    // Self-interacting terms
    double Vself = 0;
    for (int i=0; i<Qs.size(); i++)
      Vself -= Qs(i)*Qs(i)*0.5*Vl0;

    // Neutralizing Background
    double Vb = 0.;
    for (int i=0; i<N; i++)
      Vb += 0.5*N*N*fV0;

    cout << "Vs = " << Vs << ", Vl = " << Vl << ", Vself = " << Vself << ", Vbackground = " << Vb << endl;
    double V = Vs + Vl + Vself;
    cout << "V = Vs + Vl + Vself = " << V << endl;

    // Madelung Constant
    double estMad = box[0]*V/N;
    printf("Vmad = %.10e \n",estMad);
  }

  void PrintExactCoulombMadelung()
  {
    double extMad = -1.747564594633182190636212035544397403481;
    printf("Exact Coulomb Madelung constant, Vmad = %.10e \n",extMad);
  }

};

int main(int argc, char* argv[])
{
  double L = atof(argv[1]);
  dVec box(L,L,L); // Hard-coded cube
  double kCut = atof(argv[2]);
  double rMin = atof(argv[3]);
  double rCut = atof(argv[4]);
  int nPoints = atoi(argv[5]);
  int gridType = atoi(argv[6]);
  double Z1Z2 = atof(argv[7]);
  int breakupType = atoi(argv[8]);
  int breakupObject = atoi(argv[9]);
  int paIndex = atoi(argv[10]);
  int nKnots = atoi(argv[11]);
  double tau = atof(argv[12]);
  int nImages = atoi(argv[13]);

  Grid *grid;
  Array<double,1> rs(nPoints);
  if (gridType == 0) {
    grid = new LinearGrid(rMin, rCut, nPoints);
  } else if (gridType == 1) {
    grid = new LogGrid(rMin, pow(rCut/rMin,1./nPoints), nPoints);
  } else if (gridType == 2) {
    stringstream rFileName;
    rFileName << "grid." << paIndex << ".txt";
    string rFileNameStr = rFileName.str();
    ifstream pointFile;
    pointFile.open(rFileNameStr.c_str());
    Array<double,1> Vs(nPoints);
    int ri = 0;
    while (!pointFile.eof()) {
      double r, V;
      pointFile>>rs(ri);
      ri += 1;
    }
    pointFile.close();
    grid = new GeneralGrid(rs);
  }

  EwaldClass e(Z1Z2, box, kCut, rMin, rCut, nPoints, *grid, breakupType, breakupObject, paIndex, nKnots, tau);
  e.DoBreakup();
  e.ComputeMadelung();
  e.ComputeMadelungNaive(nImages);
  e.PrintExactCoulombMadelung();

}
