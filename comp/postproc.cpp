/*********************************************************************/
/* File:   postproc.cpp                                              */
/* Author: Joachim Schoeberl                                         */
/* Date:   25. Mar. 2000                                             */
/*********************************************************************/

/* 
   Postprocessing functions
*/

#include <comp.hpp>
#include <variant>
#include <optional>

namespace ngcomp
{ 

  inline void LapackInverseSPD (ngbla::SliceMatrix<Complex> a)
  {
    LapackInverse (a);
  }

  /*
  inline void LapackSolveSPD (FlatMatrix<> a, FlatVector<> rhs, FlatVector<> sol)
  {
    integer n = a.Width();
    if (n == 0) return;
    integer lda = a.Width();

    integer info;
    char uplo = 'U';

    dpotrf_ (&uplo, &n, &a(0,0), &lda, &info);
    if (info != 0)
      {
        cout << "plan b" << endl;
        CholeskyFactors<double> invelmat(a);
        invelmat.Mult (rhs, sol);
        return;
        // cout << "info1 = " << info << endl;
      }
    sol = rhs;
    integer nrhs = 1;
    integer ldb = n;
    dpotrs_ (&uplo, &n, &nrhs, &a(0,0), &lda, &sol(0), &ldb, &info);
    if (info != 0) cout << "info2 = " << info << endl;
  }

  inline void LapackSolveSPD (SliceMatrix<Complex> mat, FlatVector<Complex> rhs, FlatVector<Complex> sol)
  {
    LapackInverseSPD (mat);
    sol = mat * rhs;
  }
  */

  
  
  template <class SCAL>
  void CalcFluxProject (const S_GridFunction<SCAL> & u,
			S_GridFunction<SCAL> & flux,
			shared_ptr<BilinearFormIntegrator> bli,
			bool applyd, const BitArray & domains, LocalHeap & clh)
  {
    static int timer = NgProfiler::CreateTimer ("CalcFluxProject");
    NgProfiler::RegionTimer reg (timer);
    
    auto fes = u.GetFESpace();
    auto fesflux = flux.GetFESpace();

    shared_ptr<MeshAccess> ma = fesflux->GetMeshAccess();

    ma->PushStatus ("Post-processing");
    

    auto vb = bli->VB();

    int ne      = ma->GetNE(vb);
    int dim     = fes->GetDimension();
    int dimflux = fesflux->GetDimension();
    int dimfluxvec = bli->DimFlux(); 

    shared_ptr<BilinearFormIntegrator> fluxbli = fesflux->GetIntegrator(vb);
    shared_ptr<BilinearFormIntegrator> single_fluxbli = fluxbli;
    if (dynamic_pointer_cast<BlockBilinearFormIntegrator> (single_fluxbli))
      single_fluxbli = dynamic_pointer_cast<BlockBilinearFormIntegrator> (single_fluxbli)->BlockPtr();
    
    auto flux_evaluator = fesflux->GetEvaluator(vb);
    if (!fluxbli)
      {
        cout << IM(5) << "make a symbolic integrator for CalcFluxProject" << endl;
        auto single_evaluator =  flux_evaluator;
        if (dynamic_pointer_cast<BlockDifferentialOperator>(single_evaluator))
          single_evaluator = dynamic_pointer_cast<BlockDifferentialOperator>(single_evaluator)->BaseDiffOp();
        
        auto trial = make_shared<ProxyFunction>(fesflux, false, false, single_evaluator,
                                                nullptr, nullptr, nullptr, nullptr, nullptr);
        auto test  = make_shared<ProxyFunction>(fesflux, true, false, single_evaluator,
                                                nullptr, nullptr, nullptr, nullptr, nullptr);
        fluxbli = make_shared<SymbolicBilinearFormIntegrator> (InnerProduct(trial,test), vb, VOL);
        single_fluxbli = fluxbli;
        // throw Exception ("no integrator available");
      }

    Array<int> cnti(fesflux->GetNDof());
    cnti = 0;

    flux.GetVector() = 0.0;

    ProgressOutput progress (ma, "postprocessing element", ne);

    IterateElements  
      (*fesflux, vb, clh, 
       [&] (Ngs_Element ei, LocalHeap & lh)
       {
         HeapReset hr(lh);
         progress.Update ();
         
         if (!domains[ei.GetIndex()]) return;;

         const FiniteElement & fel = fes->GetFE (ei, lh);
         const FiniteElement & felflux = fesflux->GetFE (ei, lh);
	 
         ElementTransformation & eltrans = ma->GetTrafo (ei, lh);

         Array<int> dnums(fel.GetNDof(), lh);
         fes->GetDofNrs (ei, dnums);

         Array<int> dnumsflux(felflux.GetNDof(), lh);
         fesflux->GetDofNrs(ei, dnumsflux);

         FlatVector<SCAL> elu(dnums.Size() * dim, lh);
         FlatVector<SCAL> elflux(dnumsflux.Size() * dimflux, lh);
         FlatVector<SCAL> elfluxi(dnumsflux.Size() * dimflux, lh);
         FlatVector<SCAL> fluxi(dimfluxvec, lh);
	 
         u.GetElementVector (dnums, elu);
         fes->TransformVec (ei, elu, TRANSFORM_SOL);
         
         IntegrationRule ir(fel.ElementType(), 
                            max2(fel.Order(),felflux.Order())+felflux.Order());
         
         BaseMappedIntegrationRule & mir = eltrans(ir, lh);
         FlatMatrix<SCAL> mfluxi(ir.GetNIP(), dimfluxvec, lh);
	 
         bli->CalcFlux (fel, mir, elu, mfluxi, applyd, lh);
	 
         for (int j : Range(ir))
           mfluxi.Row(j) *= mir[j].GetWeight();
         
         elflux = 0;
         // fluxbli->ApplyBTrans (felflux, mir, mfluxi, elflux, lh);
         flux_evaluator->ApplyTrans (felflux, mir, mfluxi, elflux, lh);
         
         if (dimflux > 1) 
           {
             FlatMatrix<SCAL> elmat(dnumsflux.Size(), lh);
             single_fluxbli->CalcElementMatrix (felflux, eltrans, elmat, lh);
             FlatCholeskyFactors<SCAL> invelmat(elmat, lh);
             
             for (int j = 0; j < dimflux; j++)
               invelmat.Mult (elflux.Slice (j, dimflux), 
                              elfluxi.Slice (j, dimflux));
           }
         else
           {
             FlatMatrix<SCAL> elmat(dnumsflux.Size(), lh);
             fluxbli->CalcElementMatrix (felflux, eltrans, elmat, lh);
             FlatCholeskyFactors<SCAL> invelmat(elmat, lh);
             invelmat.Mult (elflux, elfluxi);
           }
         
         fesflux->TransformVec (ei, elfluxi, TRANSFORM_SOL);
	  
	  
         flux.GetElementVector (dnumsflux, elflux);
         elfluxi += elflux;
         flux.SetElementVector (dnumsflux, elfluxi);

         for (auto d : dnumsflux) if (IsRegularDof(d)) cnti[d]++;
       });

    // }
  

    progress.Done();
    
#ifdef PARALLEL
    AllReduceDofData (cnti, MPI_SUM, fesflux->GetParallelDofs());
    flux.GetVector().SetParallelStatus(DISTRIBUTED);
    flux.GetVector().Cumulate(); 	 
#endif

    FlatVector<SCAL> fluxi(dimflux, clh);
    ArrayMem<int,1> dnumsflux(1);
    for (int i = 0; i < cnti.Size(); i++)
      if (cnti[i])
	{
	  dnumsflux[0] = i;
	  flux.GetElementVector (dnumsflux, fluxi);
	  fluxi /= double (cnti[i]);
	  flux.SetElementVector (dnumsflux, fluxi);
	}
    
    ma->PopStatus ();
  }


  
  template <class SCAL>
  void CalcFluxProject (const S_GridFunction<SCAL> & u,
			S_GridFunction<SCAL> & flux,
			shared_ptr<BilinearFormIntegrator> bli,
			bool applyd, int domain, LocalHeap & lh)
  {
    shared_ptr<MeshAccess> ma = flux.GetFESpace()->GetMeshAccess();

    BitArray domains(ma->GetNDomains());
    
    if(domain == -1)
      domains.Set();
    else
      {
	domains.Clear();
	domains.Set(domain);
      }
    
    CalcFluxProject(u,flux,bli,applyd,domains,lh);
  }


  void CalcFluxProject (const GridFunction & bu,
			GridFunction & bflux,
			shared_ptr<BilinearFormIntegrator> bli,
			bool applyd, int domain, LocalHeap & lh)
  {
    if (bu.GetFESpace()->IsComplex())
      {
	CalcFluxProject (dynamic_cast<const S_GridFunction<Complex>&> (bu),
			 dynamic_cast<S_GridFunction<Complex>&> (bflux),
			 bli, applyd, domain, lh);
      }
    else
      {
	CalcFluxProject (dynamic_cast<const S_GridFunction<double>&> (bu),
			 dynamic_cast<S_GridFunction<double>&> (bflux),
			 bli, applyd, domain, lh);
      }
  }




  template <class SCAL> 
  int CalcPointFlux (const GridFunction & bu,
		     const FlatVector<double> & point,
		     const Array<int> & domains,
		     FlatVector<SCAL> & flux,
		     shared_ptr<BilinearFormIntegrator> bli,
		     bool applyd,
		     LocalHeap & lh,
		     int component)// = 0)
  {
    static Timer t("CalcPointFlux");
    RegionTimer reg(t);

    HeapReset hr(lh);

    int elnr;

    IntegrationPoint ip(0,0,0,1);

    bool boundary = bli->BoundaryForm();

    shared_ptr<MeshAccess> ma = bu.GetMeshAccess();

    if(boundary)
      {
	if(domains.Size() > 0)
	  elnr = ma->FindSurfaceElementOfPoint(point,ip,false,&domains);
	else
	  elnr = ma->FindSurfaceElementOfPoint(point,ip,false);
      }
    else
      {
      	if(domains.Size() > 0)
	  elnr = ma->FindElementOfPoint(point,ip,false,&domains);
	else
	  elnr = ma->FindElementOfPoint(point,ip,false);
      }
    if (elnr < 0) return 0;

    const S_GridFunction<SCAL> & u = 
      dynamic_cast<const S_GridFunction<SCAL>&> (bu);
    ElementId ei (boundary ? BND : VOL, elnr);

    const FESpace & fes = *u.GetFESpace();
    const FiniteElement & fel = fes.GetFE (ei, lh);
    const ElementTransformation & eltrans = ma->GetTrafo (ei, lh);
    Array<int> dnums(fel.GetNDof(), lh);
    fes.GetDofNrs (ei, dnums);
	
    FlatVector<SCAL> elu(dnums.Size() * fes.GetDimension(), lh);
	
    if(bu.GetCacheBlockSize() == 1)
      {
	u.GetElementVector (dnums, elu);
      }
    else
      {
	FlatVector<SCAL> elu2(dnums.Size() * fes.GetDimension() * bu.GetCacheBlockSize(), lh);
	u.GetElementVector (dnums,elu2);
	for(int i=0; i<elu.Size(); i++)
	  elu[i] = elu2[i*bu.GetCacheBlockSize()+component];
      }
    
    fes.TransformVec (ei, elu, TRANSFORM_SOL);
    bli->CalcFlux (fel, eltrans(ip, lh), elu, flux, applyd, lh);
    return 1;
  }
  

  template NGS_DLL_HEADER 
  int CalcPointFlux<double> (
                             const GridFunction & u,
                             const FlatVector<double> & point,
                             const Array<int> & domains,
                             FlatVector<double> & flux,
                             shared_ptr<BilinearFormIntegrator> bli,
                             bool applyd,
                             LocalHeap & lh,
                             int component);
  
  template NGS_DLL_HEADER 
  int CalcPointFlux<Complex> (const GridFunction & u,
                              const FlatVector<double> & point,
                              const Array<int> & domains,
                              FlatVector<Complex> & flux,
                              shared_ptr<BilinearFormIntegrator> bli,
                              bool applyd,
                              LocalHeap & lh,
                              int component);
    

  
  template <class SCAL>
  int CalcPointFlux (const GridFunction & bu,
		     const FlatVector<double> & point,
		     FlatVector<SCAL> & flux,
		     shared_ptr<BilinearFormIntegrator> bli,
		     bool applyd,
		     LocalHeap & lh,
		     int component)
  {
    Array<int> dummy;
    return CalcPointFlux(bu,point,dummy,flux,bli,applyd,lh,component);
  }



  template NGS_DLL_HEADER 
  int CalcPointFlux<double> (const GridFunction & u,
                             const FlatVector<double> & point,
                             FlatVector<double> & flux,
                             shared_ptr<BilinearFormIntegrator> bli,
                             bool applyd,
                             LocalHeap & lh,
                             int component);
  
  template NGS_DLL_HEADER 
  int CalcPointFlux<Complex> (const GridFunction & u,
                              const FlatVector<double> & point,
                              FlatVector<Complex> & flux,
                              shared_ptr<BilinearFormIntegrator> bli,
                              bool applyd,
                              LocalHeap & lh,
                              int component);
    





  template <class SCAL>
  void SetValues (shared_ptr<CoefficientFunction> coef,
		  GridFunction & u,
		  VorB vb,
                  const Region * reg, 
		  DifferentialOperator * diffop,
		  LocalHeap & clh)
  {
    static Timer sv("timer setvalues"); RegionTimer r(sv);

    auto fes = u.GetFESpace();
    shared_ptr<MeshAccess> ma = fes->GetMeshAccess(); 
    int dim   = fes->GetDimension();
    ma->PushStatus("setvalues");

    if (!diffop)
      diffop = fes->GetEvaluator(vb).get();
    shared_ptr<BilinearFormIntegrator> bli = fes->GetIntegrator(vb);
    shared_ptr<BilinearFormIntegrator> single_bli = bli;
    if (dynamic_pointer_cast<BlockBilinearFormIntegrator> (single_bli))
      single_bli = dynamic_pointer_cast<BlockBilinearFormIntegrator> (single_bli)->BlockPtr();
    
    if (!bli)
      {
        cout << IM(5) << "make a symbolic integrator for interpolation" << endl;
        if (!fes->GetEvaluator(vb))
          throw Exception(fes->GetClassName()+string(" does not have an evaluator for ")+ToString(vb)+string("!"));
        auto single_evaluator =  fes->GetEvaluator(vb);
        if (dynamic_pointer_cast<BlockDifferentialOperator>(single_evaluator))
          single_evaluator = dynamic_pointer_cast<BlockDifferentialOperator>(single_evaluator)->BaseDiffOp();
        
        auto trial = make_shared<ProxyFunction>(fes, false, false, single_evaluator,
                                                nullptr, nullptr, nullptr, nullptr, nullptr);
        auto test  = make_shared<ProxyFunction>(fes, true, false, single_evaluator,
                                                nullptr, nullptr, nullptr, nullptr, nullptr);
        bli = make_shared<SymbolicBilinearFormIntegrator> (InnerProduct(trial,test), vb, VOL);
        single_bli = bli;
        // throw Exception ("no integrator available");
      }

    int dimflux = diffop ? diffop->Dim() : bli->DimFlux(); 
    if (coef -> Dimension() != dimflux)
      throw Exception(string("Error in SetValues: gridfunction-dim = ") + ToString(dimflux) +
                      ", but coefficient-dim = " + ToString(coef->Dimension()));
    Array<int> cnti(fes->GetNDof());
    cnti = 0;

    u.GetVector() = 0.0;

    ProgressOutput progress (ma, "setvalues element", ma->GetNE(vb));
    bool use_simd = true;

    
    IterateElements 
      (*fes, vb, clh, 
       [&] (FESpace::Element ei, LocalHeap & lh)
       {
          progress.Update ();

          if (reg)
            {
              if (!reg->Mask().Test(ei.GetIndex())) return;
            }
          else
            {
              if (vb==BND && !fes->IsDirichletBoundary(ei.GetIndex()))
                return;
            }
          
	  const FiniteElement & fel = fes->GetFE (ei, lh);
	  const ElementTransformation & eltrans = ma->GetTrafo (ei, lh); 

          // Array<int> dnums(fel.GetNDof(), lh);
          // fes.GetDofNrs (ei, dnums);

	  FlatVector<SCAL> elflux(fel.GetNDof() * dim, lh);
	  FlatVector<SCAL> elfluxi(fel.GetNDof() * dim, lh);
	  FlatVector<SCAL> fluxi(dimflux, lh);

          if (use_simd)
            {
              try
                {
                  SIMD_IntegrationRule ir(fel.ElementType(), 2*fel.Order());
                  FlatMatrix<SIMD<SCAL>> mfluxi(dimflux, ir.Size(), lh);
                  
                  auto & mir = eltrans(ir, lh);

                  coef->Evaluate (mir, mfluxi);
          
                  for (size_t j : Range(ir))
                    mfluxi.Col(j) *= mir[j].GetWeight();

                  elflux = SCAL(0.0);
                  if (diffop)
                    diffop -> AddTrans (fel, mir, mfluxi, elflux);
                  else
                    throw ExceptionNOSIMD("need diffop");

                  if (dim > 1) //  && typeid(*bli)==typeid(BlockBilinearFormIntegrator))
                    {
                      FlatMatrix<SCAL> elmat(fel.GetNDof(), lh);
                      single_bli->CalcElementMatrix (fel, eltrans, elmat, lh);                      
                      FlatCholeskyFactors<SCAL> invelmat(elmat, lh);
                      
                      for (int j = 0; j < dim; j++)
                        invelmat.Mult (elflux.Slice (j,dim), elfluxi.Slice (j,dim));
                    }
                  else
                    {
                      FlatMatrix<SCAL> elmat(fel.GetNDof(), lh);
                      bli->CalcElementMatrix (fel, eltrans, elmat, lh);
                      
                      fes->TransformMat (ei, elmat, TRANSFORM_MAT_LEFT_RIGHT);
                      fes->TransformVec (ei, elflux, TRANSFORM_RHS);
                      // if (fel.GetNDof() < 50)
                      if (true)
                        {
                          // FlatCholeskyFactors<SCAL> invelmat(elmat, lh);
                          // invelmat.Mult (elflux, elfluxi);

                          CalcLDL<SCAL,ColMajor> (Trans(elmat));
                          elfluxi = elflux;
                          SolveLDL<SCAL,ColMajor> (Trans(elmat), elfluxi);
                        }
                      else
                        {
                          LapackInverseSPD (elmat);
                          elfluxi = elmat * elflux;
                        }
                    }
                  
                  // fes.TransformVec (i, bound, elfluxi, TRANSFORM_SOL);
                  
                  u.GetElementVector (ei.GetDofs(), elflux);
                  elfluxi += elflux;
                  u.SetElementVector (ei.GetDofs(), elfluxi);
                  
                  for (auto d : ei.GetDofs())
                    if (IsRegularDof(d)) cnti[d]++;
                  
                  return;
                }
              catch (ExceptionNOSIMD e)
                {
                  use_simd = false;
                  cout << IM(4) << "Warning: switching to std evalution in SetValues since: " << e.What() << endl;
                }
            }
          
	  IntegrationRule ir(fel.ElementType(), 2*fel.Order());
	  FlatMatrix<SCAL> mfluxi(ir.GetNIP(), dimflux, lh);

	  BaseMappedIntegrationRule & mir = eltrans(ir, lh);

	  coef->Evaluate (mir, mfluxi);
          
	  for (int j : Range(ir))
	    mfluxi.Row(j) *= mir[j].GetWeight();

	  if (diffop)
	    diffop -> ApplyTrans (fel, mir, mfluxi, elflux, lh);
	  else
	    bli->ApplyBTrans (fel, mir, mfluxi, elflux, lh);

	  if (dim > 1)
	    {
	      FlatMatrix<SCAL> elmat(fel.GetNDof(), lh);
	      // const BlockBilinearFormIntegrator & bbli = 
              // dynamic_cast<const BlockBilinearFormIntegrator&> (*bli.get());
	      // bbli . Block() . CalcElementMatrix (fel, eltrans, elmat, lh);
              single_bli->CalcElementMatrix (fel, eltrans, elmat, lh);
	      FlatCholeskyFactors<SCAL> invelmat(elmat, lh);
              
	      for (int j = 0; j < dim; j++)
                invelmat.Mult (elflux.Slice (j,dim), elfluxi.Slice (j,dim));
	    }
	  else
	    {
	      FlatMatrix<SCAL> elmat(fel.GetNDof()*dim, lh);
	      bli->CalcElementMatrix (fel, eltrans, elmat, lh);

	      fes->TransformMat (ei, elmat, TRANSFORM_MAT_LEFT_RIGHT);
	      fes->TransformVec (ei, elflux, TRANSFORM_RHS);
              // if (fel.GetNDof() < 50)
              if (true)
                {
                  // FlatCholeskyFactors<SCAL> invelmat(elmat, lh);
                  // invelmat.Mult (elflux, elfluxi);

                  CalcLDL<SCAL,ColMajor> (Trans(elmat));
                  elfluxi = elflux;
                  SolveLDL<SCAL,ColMajor> (Trans(elmat), elfluxi);
                }
              else
                {
                  LapackInverse (elmat);
                  elfluxi = elmat * elflux;
                }
	    }

	  // fes.TransformVec (i, bound, elfluxi, TRANSFORM_SOL);

          u.GetElementVector (ei.GetDofs(), elflux);
          elfluxi += elflux;
          u.SetElementVector (ei.GetDofs(), elfluxi);
	  
          for (auto d : ei.GetDofs())
            if (d != -1) cnti[d]++;

       });

    progress.Done();

    
#ifdef PARALLEL
    AllReduceDofData (cnti, MPI_SUM, fes->GetParallelDofs());
    u.GetVector().SetParallelStatus(DISTRIBUTED);
    u.GetVector().Cumulate(); 	 
#endif

    ParallelForRange
      (cnti.Size(), [&] (IntRange r)
       {
         VectorMem<10,SCAL> fluxi(dim);
         ArrayMem<int,1> dnums(1);
         // for (int i = 0; i < cnti.Size(); i++)
         for (auto i : r)
           if (cnti[i])
             {
               dnums[0] = i;
               u.GetElementVector (dnums, fluxi);
               fluxi /= double (cnti[i]);
               u.SetElementVector (dnums, fluxi);
             }
       });
    
    ma->PopStatus ();
  }
  
  NGS_DLL_HEADER void SetValues (shared_ptr<CoefficientFunction> coef,
				 GridFunction & u,
				 VorB vb,
				 DifferentialOperator * diffop,
				 LocalHeap & clh)
  {
    if (u.GetFESpace()->IsComplex())
      SetValues<Complex> (coef, u, vb, nullptr, diffop, clh);
    else
      SetValues<double> (coef, u, vb, nullptr, diffop, clh);
  }

  NGS_DLL_HEADER void SetValues (shared_ptr<CoefficientFunction> coef,
				 GridFunction & u,
				 const Region & reg, 
				 DifferentialOperator * diffop,
				 LocalHeap & clh)
  {
    if (u.GetFESpace()->IsComplex())
      SetValues<Complex> (coef, u, reg.VB(), &reg, diffop, clh);
    else
      SetValues<double> (coef, u, reg.VB(), &reg, diffop, clh);
  }




  template <class SCAL>
  void CalcError (const S_GridFunction<SCAL> & u,
		  const S_GridFunction<SCAL> & flux,
		  shared_ptr<BilinearFormIntegrator> bli,
		  FlatVector<double> & err,
		  const BitArray & domains, LocalHeap & lh)
  {
    static int timer = NgProfiler::CreateTimer ("CalcError");
    NgProfiler::RegionTimer reg (timer);

    shared_ptr<MeshAccess> ma = u.GetMeshAccess();

    ma->PushStatus ("Error estimator");

    const FESpace & fes = *u.GetFESpace();
    const FESpace & fesflux = *flux.GetFESpace();

    VorB vb = bli->VB();

    if(vb==BBND)
      throw Exception("CalcError not implemented for co dim 2");

    int ne      = ma->GetNE(vb);
    int dim     = fes.GetDimension();
    int dimflux = fesflux.GetDimension();
    int dimfluxvec = bli->DimFlux(); // fesflux.GetDimension();

    // shared_ptr<BilinearFormIntegrator> fluxbli = fesflux.GetIntegrator(vb);
    shared_ptr<DifferentialOperator> flux_diffop = fesflux.GetEvaluator(vb);

    Array<int> dnums;
    Array<int> dnumsflux;

    double sum = 0;
    for (int i = 0; i < ne; i++)
      {
        ElementId ei(vb,i);

	HeapReset hr(lh);
	ma->SetThreadPercentage ( 100.0*i / ne );

	int eldom = ma->GetElIndex(ei);
        // bound ? ma->GetSElIndex(i) : ma->GetElIndex(i);
	
	if (!domains[eldom]) continue;

	const FiniteElement & fel = fes.GetFE(ei, lh);
	const FiniteElement & felflux = fesflux.GetFE(ei, lh);

	ElementTransformation & eltrans = ma->GetTrafo (ei, lh);
	fes.GetDofNrs(ei,dnums);
	fesflux.GetDofNrs(ei,dnumsflux);

	FlatVector<SCAL> elu(dnums.Size() * dim, lh);
	FlatVector<SCAL> elflux(dnumsflux.Size() * dimflux, lh);
	FlatVector<SCAL> fluxi(dimfluxvec, lh);
	FlatVector<SCAL> fluxi2(dimfluxvec, lh);


	u.GetElementVector (dnums, elu);
	fes.TransformVec (ei, elu, TRANSFORM_SOL);
	flux.GetElementVector (dnumsflux, elflux);
	fesflux.TransformVec (ei, elflux, TRANSFORM_SOL);

	IntegrationRule ir(felflux.ElementType(), 2*felflux.Order());

	FlatMatrix<SCAL> mfluxi(ir.GetNIP(), dimfluxvec, lh);
	FlatMatrix<SCAL> mfluxi2(ir.GetNIP(), dimfluxvec, lh);
	
	BaseMappedIntegrationRule & mir = eltrans(ir, lh);
	bli->CalcFlux (fel, mir, elu, mfluxi, 1, lh);
        // fluxbli->CalcFlux (felflux, mir, elflux, mfluxi2, 0, lh);
        // cout << "mfluxi2 - bli = " << mfluxi2 << endl;
        // mfluxi2 = 0;
        flux_diffop->Apply (felflux, mir, elflux, mfluxi2, lh);
        // cout << "mfluxi2 - diffop = " << mfluxi2 << endl;
        
	mfluxi -= mfluxi2;
	
	bli->ApplyDMatInv (fel, mir, mfluxi, mfluxi2, lh);
	
	double elerr = 0;
	for (int j = 0; j < ir.GetNIP(); j++)
	  elerr += ir[j].Weight() * mir[j].GetMeasure() *
	    fabs (InnerProduct (mfluxi.Row(j), mfluxi2.Row(j)));

	err(i) += elerr;
	sum += elerr;
      }
    ma->PopStatus ();
  }
  

  
  template <class SCAL>
  void CalcError (const S_GridFunction<SCAL> & u,
		  const S_GridFunction<SCAL> & flux,
		  shared_ptr<BilinearFormIntegrator> bli,
		  FlatVector<double> & err,
		  int domain, LocalHeap & lh)
  {
    BitArray domains(u.GetMeshAccess()->GetNDomains());
    
    if(domain == -1)
      domains.Set();
    else
      {
	domains.Clear();
	domains.Set(domain);
      }

    CalcError(u,flux,bli,err,domains,lh);    
  }


  void CalcError (const GridFunction & bu,
		  const GridFunction & bflux,
		  shared_ptr<BilinearFormIntegrator> bli,
		  FlatVector<double> & err,
		  int domain, LocalHeap & lh)
  {
    if (bu.GetFESpace()->IsComplex())
      {
	CalcError (dynamic_cast<const S_GridFunction<Complex>&> (bu),
		   dynamic_cast<const S_GridFunction<Complex>&> (bflux),
		   bli, err, domain, lh);
      }
    else
      {
	CalcError (dynamic_cast<const S_GridFunction<double>&> (bu),
		   dynamic_cast<const S_GridFunction<double>&> (bflux),
		   bli, err, domain, lh);
      }
  }
  

  template <class SCAL>
  void CalcDifference (const S_GridFunction<SCAL> & u1,
		       const S_GridFunction<SCAL> & u2,
		       shared_ptr<BilinearFormIntegrator> bli1,
		       shared_ptr<BilinearFormIntegrator> bli2,
		       FlatVector<double> & diff,
		       int domain, LocalHeap & lh)
  {
    shared_ptr<MeshAccess> ma = u1.GetMeshAccess();
    ma->PushStatus ("Calc Difference");

    const FESpace & fes1 = *u1.GetFESpace();
    const FESpace & fes2 = *u2.GetFESpace();

    bool bound1 = bli1->BoundaryForm();
    bool bound2 = bli2->BoundaryForm();


    if(bound1!=bound2) 
      {
	cout << " ERROR: CalcDifference :: bli1->BoundaryForm != bl2.BoundaryForm there is something wrong?" << endl; 
	diff = 0; 
	return; 
      } 

    int ne      = bound1 ? ma->GetNSE() : ma->GetNE();
    int dim1    = fes1.GetDimension();
    int dim2    = fes2.GetDimension();
    int dimflux1 = bli1->DimFlux();
    int dimflux2 = bli2->DimFlux();

    if(dimflux1 != dimflux2) 
      { 
	cout << " ERROR: CalcDifference :: dimflux1 != dimflux2 !!!!! -> set diff = 0" << endl; 
	diff = 0; 
	return; 	
      } 

    bool applyd1 = 0;
    bool applyd2 = 0;

    Array<int> dnums1;
    Array<int> dnums2;

    double sum = 0;
    for (int i = 0; i < ne; i++)
      {
        ElementId ei(bound1 ? BND : VOL, i);
	HeapReset hr (lh);
	ma->SetThreadPercentage ( 100.0*i / ne );

	int eldom = ma->GetElIndex(ei);
        // bound1 ? ma->GetSElIndex(i) : ma->GetElIndex(i);
	
	if ((domain != -1) && (domain != eldom))
	  continue;

	const FiniteElement & fel1 = fes1.GetFE (ei, lh);
	const FiniteElement & fel2 = fes2.GetFE (ei, lh);
	ElementTransformation & eltrans = ma->GetTrafo (ei, lh);

	fes1.GetDofNrs (ei, dnums1);
	fes2.GetDofNrs (ei, dnums2);

	FlatVector<SCAL> elu1(dnums1.Size() * dim1, lh);
	FlatVector<SCAL> elu2(dnums2.Size() * dim2, lh);
	FlatVector<SCAL> fluxi1(dimflux1, lh);
	FlatVector<SCAL> fluxi2(dimflux2, lh);


	u1.GetElementVector (dnums1, elu1);
	fes1.TransformVec (ei, elu1, TRANSFORM_SOL);
	u2.GetElementVector (dnums2, elu2);
	fes2.TransformVec (ei, elu2, TRANSFORM_SOL);

	double elerr = 0;

	int io = max2(fel1.Order(),fel2.Order()); 

	IntegrationRule ir(fel1.ElementType(), 2*io+2);
	BaseMappedIntegrationRule & mir = eltrans(ir, lh);
	
	for (int j = 0; j < ir.GetNIP(); j++)
	  {
	    HeapReset hr (lh);
	    
	    bli1->CalcFlux (fel1, mir[j], elu1, fluxi1, applyd1, lh);
	    bli2->CalcFlux (fel2, mir[j], elu2, fluxi2, applyd2, lh);
	    // double det = mir[j].GetMeasure();
	    
	    fluxi1 -= fluxi2;
	     
	    double dx = mir[j].GetWeight();
	    elerr += dx * L2Norm2 (fluxi1);
	  }

	diff(i) += elerr;
	sum += elerr;
      }
    // cout << "difference = " << sqrt(sum) << endl;
    ma->PopStatus ();
  }
  
  template void CalcDifference<double> (const S_GridFunction<double> & bu1,
					const S_GridFunction<double> & bu2,
					shared_ptr<BilinearFormIntegrator> bli1,
					shared_ptr<BilinearFormIntegrator> bli2,
					FlatVector<double> & err,
					int domain, LocalHeap & lh);
  
  template void CalcDifference<Complex> (const S_GridFunction<Complex> & bu1,
					 const S_GridFunction<Complex> & bu2,
					 shared_ptr<BilinearFormIntegrator> bli1,
					 shared_ptr<BilinearFormIntegrator> bli2,
					 FlatVector<double> & err,
					 int domain, LocalHeap & lh);    
  




  template <class SCAL>
  void CalcDifference (const S_GridFunction<SCAL> & u1,
		       shared_ptr<BilinearFormIntegrator> bli1,
		       shared_ptr<CoefficientFunction> coef, 
		       FlatVector<double> & diff,
		       int domain, LocalHeap & lh)
  {
    shared_ptr<MeshAccess> ma = u1.GetMeshAccess();

    ma->PushStatus ("Calc Difference");

    const FESpace & fes1 = *u1.GetFESpace();

    bool bound1 = bli1->BoundaryForm();


    int ne      = bound1 ? ma->GetNSE() : ma->GetNE();
    int dim1    = fes1.GetDimension();
    int dimflux1 = bli1->DimFlux();

    bool applyd1 = 0;

    Array<int> dnums1;

    double sum = 0;
    for (int i = 0; i < ne; i++)
      {
        ElementId ei(bound1 ? BND : VOL, i);

	ma->SetThreadPercentage ( 100.0*i / ne );
        
	lh.CleanUp();

	int eldom = ma->GetElIndex(ei);
	
	if ((domain != -1) && (domain != eldom))
	  continue;

	const FiniteElement & fel1 = fes1.GetFE(ei, lh);

	ElementTransformation & eltrans = ma->GetTrafo (ei, lh);
	fes1.GetDofNrs (ei, dnums1);
	/*
	if (bound1)
	  {
	    ma->GetSurfaceElementTransformation (i, eltrans, lh);
	    fes1.GetSDofNrs (i, dnums1);
	  }
	else
	  {
	    ma->GetElementTransformation (i, eltrans, lh);
	    fes1.GetDofNrs (i, dnums1);
	  }
	*/

	FlatVector<SCAL> elu1(dnums1.Size() * dim1, lh);
	FlatVector<SCAL> fluxi1(dimflux1, lh);
	FlatVector<SCAL> fluxi2(dimflux1, lh);


	u1.GetElementVector (dnums1, elu1);
	fes1.TransformVec (ei, elu1, TRANSFORM_SOL);

	double elerr = 0;

	IntegrationRule ir(fel1.ElementType(), 2*fel1.Order()+3);
	double det = 0;
	
	if (bound1) 
	  throw Exception ("CalcDifference on boundary not supported");

	if (ma->GetDimension() == 2)
	  {
	    MappedIntegrationRule<2,2> mir(ir, eltrans, lh);
	    FlatMatrix<SCAL> mfluxi(ir.GetNIP(), dimflux1, lh);
	    FlatMatrix<SCAL> mfluxi2(ir.GetNIP(), dimflux1, lh);
	    
	    bli1->CalcFlux (fel1, mir, elu1, mfluxi, applyd1, lh);
	    // coef->Evaluate(mir, mfluxi2);
	    
	    for (int j = 0; j < ir.GetNIP(); j++)
	      {
		coef->Evaluate(mir[j],fluxi2);
		mfluxi2.Row(j) = fluxi2;
	      }
	    mfluxi -= mfluxi2;

	    for (int j = 0; j < ir.GetNIP(); j++)
	      {
		double dx = fabs (mir[j].GetJacobiDet()) * ir[j].Weight();
		elerr += dx * L2Norm2 (mfluxi.Row(j));
	      }
	    

	    diff(i) += elerr;
	    sum += elerr;
	  }

	else
	  {
	    for (int j = 0; j < ir.GetNIP(); j++)
	      {
		HeapReset hr(lh);
		if (!bound1)
		  {
		    if (ma->GetDimension() == 2)
		      {
			Vec<2> point; 
			MappedIntegrationPoint<2,2> mip (ir[j], eltrans);
			eltrans.CalcPoint(mip.IP(), point);
			bli1->CalcFlux (fel1, mip, elu1, fluxi1, applyd1, lh);
			coef->Evaluate(mip,fluxi2);
			det = fabs(mip.GetJacobiDet()); 
		      }
		    else
		      {
			Vec<3> point;
			MappedIntegrationPoint<3,3> mip (ir[j], eltrans);
			eltrans.CalcPoint(mip.IP(), point);
			bli1->CalcFlux (fel1, mip, elu1, fluxi1, applyd1, lh);
			coef->Evaluate(mip,fluxi2);
			det = fabs(mip.GetJacobiDet());  
		      }
		  }

	    	  
		(*testout) << "diff: fluxi = " << fluxi1 << " =?= " << fluxi2 << endl;
	    
		fluxi1 -= fluxi2;
	     
		double dx = ir[j].Weight() * det; 
	    
		elerr += dx * L2Norm2 (fluxi1);
	      }

	    diff(i) += elerr;
	    sum += elerr;
	  }
      }
    cout << "difference = " << sqrt(sum) << endl;
    ma->PopStatus ();
  }

  NGS_DLL_HEADER void CalcDifference (const GridFunction & u1,
				      shared_ptr<BilinearFormIntegrator> bfi1,
				      shared_ptr<CoefficientFunction> coef, 
				      FlatVector<double> & diff,
				      int domain, LocalHeap & lh)
  {
    if (u1.GetFESpace()->IsComplex())
      CalcDifference<Complex> (dynamic_cast<const S_GridFunction<Complex>&> (u1), 
                               bfi1, coef, diff, domain, lh);
    else
      CalcDifference<double> (dynamic_cast<const S_GridFunction<double>&> (u1), 
                              bfi1, coef, diff, domain, lh);
  }









  /*

    // not supported anymore. required fixed-order elements

  template <class SCAL>
  void CalcGradient (shared_ptr<MeshAccess> ma,
		     const FESpace & fesh1,
		     const S_BaseVector<SCAL> & vech1,
		     const FESpace & feshcurl,
		     S_BaseVector<SCAL> & vechcurl)
  {
    cout << "CalcGrad" << endl;
    const ScalarFiniteElement<2> * h1fe2d;
    const ScalarFiniteElement<3> * h1fe3d;
    const HCurlFiniteElement<2> * hcurlfe2d;
    const HCurlFiniteElement<3> * hcurlfe3d;

    h1fe2d = dynamic_cast<const ScalarFiniteElement<2>*> (&fesh1.GetFE(ET_TRIG));
    hcurlfe2d = dynamic_cast<const HCurlFiniteElement<2>*> (&feshcurl.GetFE(ET_TRIG));
    Matrix<> gradtrig(hcurlfe2d->GetNDof(), h1fe2d->GetNDof());
    ComputeGradientMatrix<2> (*h1fe2d, *hcurlfe2d, gradtrig);
    (*testout) << "gradtrig = " << gradtrig << endl;

    h1fe3d = dynamic_cast<const ScalarFiniteElement<3>*> (&fesh1.GetFE(ET_TET));
    hcurlfe3d = dynamic_cast<const HCurlFiniteElement<3>*> (&feshcurl.GetFE(ET_TET));
    Matrix<> gradtet(hcurlfe3d->GetNDof(), h1fe3d->GetNDof());
    ComputeGradientMatrix<3> (*h1fe3d, *hcurlfe3d, gradtet);
    (*testout) << "gradtet = " << gradtet << endl;


    int ne = ma->GetNE();
    Array<int> dnumsh1, dnumshcurl;
    LocalHeap lh(100000, "CalcGradient");
    
    for (int i = 0; i < ne; i++)
      {
	lh.CleanUp();
        ElementId ei(VOL, i);
	fesh1.GetDofNrs (ei, dnumsh1);
	feshcurl.GetDofNrs (ei, dnumshcurl);

	FlatVector<SCAL> elhcurl(dnumshcurl.Size(), lh);
	FlatVector<SCAL> elh1(dnumsh1.Size(), lh);



	vech1.GetIndirect (dnumsh1, elh1);
	fesh1.TransformVec (ei, elh1, TRANSFORM_RHS);

	switch (fesh1.GetFE(ei, lh).ElementType())
	  {
	  case ET_TRIG:
	    elhcurl = gradtrig * elh1;
	    break;
	  case ET_TET:
	    elhcurl = gradtet * elh1;
	    break;
	  default:
	    throw Exception ("CalcGradient: unsupported element");
	  }

	feshcurl.TransformVec (ei, elhcurl, TRANSFORM_RHS);
	vechcurl.SetIndirect (dnumshcurl, elhcurl);
      }
  }
  
  template
  void CalcGradient<double> (shared_ptr<MeshAccess> ma,
			     const FESpace & fesh1,
			     const S_BaseVector<double> & vech1,
			     const FESpace & feshcurl,
			     S_BaseVector<double> & vechcurl);







  
  template <class SCAL>
  void CalcGradientT (shared_ptr<MeshAccess> ma,
		      const FESpace & feshcurl,
		      const S_BaseVector<SCAL> & vechcurl1,
		      const FESpace & fesh1,
		      S_BaseVector<SCAL> & vech1)
  {
    cout << "CalcGrad" << endl;
    const ScalarFiniteElement<2> * h1fe2d;
    const ScalarFiniteElement<3> * h1fe3d;
    const HCurlFiniteElement<2> * hcurlfe2d;
    const HCurlFiniteElement<3> * hcurlfe3d;

    h1fe2d = dynamic_cast<const ScalarFiniteElement<2>*> (&fesh1.GetFE(ET_TRIG));
    hcurlfe2d = dynamic_cast<const HCurlFiniteElement<2>*> (&feshcurl.GetFE(ET_TRIG));
    Matrix<> gradtrig(hcurlfe2d->GetNDof(), h1fe2d->GetNDof());
    ComputeGradientMatrix<2> (*h1fe2d, *hcurlfe2d, gradtrig);
    (*testout) << "gradtrig = " << gradtrig << endl;

    h1fe3d = dynamic_cast<const ScalarFiniteElement<3>*> (&fesh1.GetFE(ET_TET));
    hcurlfe3d = dynamic_cast<const HCurlFiniteElement<3>*> (&feshcurl.GetFE(ET_TET));
    Matrix<> gradtet(hcurlfe3d->GetNDof(), h1fe3d->GetNDof());
    ComputeGradientMatrix<3> (*h1fe3d, *hcurlfe3d, gradtet);
    (*testout) << "gradtet = " << gradtet << endl;


    S_BaseVector<SCAL> & vechcurl =
      dynamic_cast<S_BaseVector<SCAL>&> (*vechcurl1.CreateVector());

    int ne = ma->GetNE();
    Array<int> dnumsh1, dnumshcurl;
    LocalHeap lh(100000, "CalcGradientT");
    
    vechcurl = vechcurl1;
    vech1.SetScalar(0); //  = SCAL(0);
    for (int i = 0; i < ne; i++)
      {
	lh.CleanUp();
        ElementId ei(VOL, i);
	fesh1.GetDofNrs (ei, dnumsh1);
	feshcurl.GetDofNrs (ei, dnumshcurl);

	FlatVector<SCAL> elhcurl(dnumshcurl.Size(), lh);
	FlatVector<SCAL> elh1(dnumsh1.Size(), lh);

	vechcurl.GetIndirect (dnumshcurl, elhcurl);
	feshcurl.TransformVec (ei, elhcurl, TRANSFORM_RHS);

	switch (fesh1.GetFE(ei, lh).ElementType())
	  {
	  case ET_TRIG:
	    elh1 = Trans (gradtrig) * elhcurl;
	    break;
	  case ET_TET:
	    elh1 = Trans (gradtet) * elhcurl;
	    break;
	  default:
	    throw Exception ("CalcGradientT: unsupported element");
	  }

	fesh1.TransformVec (ei, elh1, TRANSFORM_RHS);
	vech1.AddIndirect (dnumsh1, elh1);

	elhcurl = 0;
	vechcurl.SetIndirect (dnumshcurl, elhcurl);
      }
  }
  
  template
  void CalcGradientT<double> (shared_ptr<MeshAccess> ma,
			      const FESpace & feshcurl,
			      const S_BaseVector<double> & vechcurl,
			      const FESpace & fesh1,
			      S_BaseVector<double> & vech1);

*/
  
}



#include "../fem/integratorcf.hpp"

namespace ngfem
{
  using namespace ngcomp;
  
  template <typename TSCAL>
  TSCAL Integral :: Integrate (const ngcomp::MeshAccess & ma)
  {
    LocalHeap glh(1000000, "integrate-lh");
    bool use_simd = true;
    TSCAL sum = 0.0;
    
    ma.IterateElements
      (this->dx.vb, glh, [&] (Ngs_Element el, LocalHeap & lh)
       {
         // if(!mask.Test(el.GetIndex())) return;
         auto & trafo = ma.GetTrafo (el, lh);
         TSCAL hsum = 0.0;
         
         bool this_simd = use_simd;
         int order = 5;
         
         if (this_simd)
           {
             try
               {
                 SIMD_IntegrationRule ir(trafo.GetElementType(), order);
                 auto & mir = trafo(ir, lh);
                 FlatMatrix<SIMD<double>> values(1, ir.Size(), lh);
                 cf -> Evaluate (mir, values);
                 SIMD<double> vsum = 0.0;
                 for (size_t i = 0; i < values.Width(); i++)
                   vsum += mir[i].GetWeight() * values(0,i);
                 hsum = HSum(vsum);
               }
             catch (ExceptionNOSIMD e)
               {
                 this_simd = false;
                 use_simd = false;
                 hsum = 0.0;
               }
           }
         if (!this_simd)
           {
             IntegrationRule ir(trafo.GetElementType(), order);
             BaseMappedIntegrationRule & mir = trafo(ir, lh);
             FlatMatrix<> values(ir.Size(), 1, lh);
             cf -> Evaluate (mir, values);
             for (int i = 0; i < values.Height(); i++)
               hsum += mir[i].GetWeight() * values(i,0);
           }
         MyAtomicAdd(sum, hsum);
       });
    
    return sum;
  }


  template double Integral :: Integrate<double> (const ngcomp::MeshAccess & ma);
  template Complex Integral :: Integrate<Complex> (const ngcomp::MeshAccess & ma);
}

