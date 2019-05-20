// WIP, need a bit more design ...

namespace ngfem
{
  class DifferentialSymbol
  {
  public:
    VorB vb;
    VorB element_vb = VOL;
    bool skeleton = false;
    BitArray definedon;
    int bonus_intorder = 0;
    DifferentialSymbol (VorB _vb) : vb(_vb) { ; }
    DifferentialSymbol (VorB _vb, VorB _element_vb, const BitArray & _definedon,
                        int _bonus_intorder)
      : vb(_vb), element_vb(_element_vb), definedon(_definedon), bonus_intorder(_bonus_intorder) { ; } 
  };
  

  class Integral
  {
  public:
    shared_ptr<CoefficientFunction> cf;
    DifferentialSymbol dx;
    Integral (shared_ptr<CoefficientFunction> _cf,
              DifferentialSymbol _dx)
      : cf(_cf), dx(_dx) { ; } 
  };
  
  inline Integral operator* (double fac, const Integral & cf)
  {
    return Integral (fac * cf.cf, cf.dx);
  }
  
  class SumOfIntegrals
  {
  public:
    Array<shared_ptr<Integral>> icfs;
    
    SumOfIntegrals() = default;
    SumOfIntegrals (shared_ptr<Integral> icf)
    { icfs += icf; }

    shared_ptr<SumOfIntegrals>
    Derive (shared_ptr<CoefficientFunction> var,
            shared_ptr<CoefficientFunction> dir) const
    {
      auto deriv = make_shared<SumOfIntegrals>();
      for (auto & icf : icfs)
        deriv->icfs += make_shared<Integral> (icf->cf->Derive(var.get(), dir), icf->dx);
      return deriv;
    }

    shared_ptr<SumOfIntegrals>
    Compile (bool realcompile, bool wait) const
    {
      auto compiled = make_shared<SumOfIntegrals>();
      for (auto & icf : icfs)
        compiled->icfs += make_shared<Integral> (::ngfem::Compile (icf->cf, realcompile, 2, wait), icf->dx);
      return compiled;
    }
  };
}