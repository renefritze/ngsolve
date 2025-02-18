/*********************************************************************/
/* File:   coefficient.cpp                                           */
/* Author: Joachim Schoeberl                                         */
/* Date:   24. Jan. 2002                                             */
/*********************************************************************/

/* 
   Finite Element Coefficient Function
*/

#include <fem.hpp>
#include <../ngstd/evalfunc.hpp>
#include <algorithm>

namespace ngstd
{

  INLINE Complex IfPos (Complex a, Complex b, Complex c)
  {
    return Complex (IfPos (a.real(), b.real(), c.real()),
                    IfPos (a.real(), b.imag(), c.imag()));
  }
  
  INLINE SIMD<Complex> IfPos (SIMD<Complex> a, SIMD<Complex> b, SIMD<Complex> c)
  {
    return SIMD<Complex> (IfPos (a.real(), b.real(), c.real()),
                          IfPos (a.real(), b.imag(), c.imag()));
  }
}


namespace ngfem
{
  
  CoefficientFunction :: ~CoefficientFunction ()
  { ; }


  void CoefficientFunction :: GenerateCode(Code &code, FlatArray<int> inputs, int index) const
  {
    string mycode =
      string("// GenerateCode() not overloaded for: ") + Demangle(typeid(*this).name()) + "\n"
      + R"CODE_(    typedef {scal_type} TStack{index};
    STACK_ARRAY(TStack{index}, hmem{index}, mir.Size()*{dim});
    {values_type} {values}({rows}, {cols}, reinterpret_cast<{scal_type}*>(&hmem{index}[0]));
    {
      const CoefficientFunction & cf = *reinterpret_cast<CoefficientFunction*>({this});
      {values} = {scal_type}(0.0);
      cf.Evaluate(mir, {values});
    }
    )CODE_";
    auto values = Var("values", index);
    string scal_type = code.res_type;
    string rows = ToString(Dimension());
    string cols = "mir.IR().Size()";

    std::map<string,string> variables;
    variables["scal_type"] = scal_type;
    variables["values_type"] = "FlatMatrix<"+scal_type+">";
    variables["values"] = values.S();
    variables["this"] =  code.AddPointer(this);
    variables["dim"] = ToString(Dimension());
    variables["index"] = ToString(index);
    variables["rows"] = code.is_simd ? rows : cols;
    variables["cols"] = code.is_simd ? cols : rows;
    code.header += Code::Map(mycode, variables);
    if(code.is_simd)
      {
        TraverseDimensions( Dimensions(), [&](int ind, int i, int j) {
            code.body += Var(index,i,j).Assign(values.S()+"("+ToString(ind)+",i)");
          });
      }
    else
      {
        TraverseDimensions( Dimensions(), [&](int ind, int i, int j) {
            code.body += Var(index,i,j).Assign(values.S()+"(i,"+ToString(ind)+")");
          });
      }
  }

  void CoefficientFunction :: PrintReport (ostream & ost) const
  {
    // ost << "CoefficientFunction is " << typeid(*this).name() << endl;
    PrintReportRec (ost, 0);
  }
  
  void CoefficientFunction :: PrintReportRec (ostream & ost, int level) const
  {
    for (int i = 0; i < 2*level; i++)
      ost << ' ';
    ost << "coef " << GetDescription() << ","
        << (IsComplex() ? " complex" : " real");
    if (Dimensions().Size() == 1)
      ost << ", dim=" << Dimension();
    else if (Dimensions().Size() == 2)
      ost << ", dims = " << Dimensions()[0] << " x " << Dimensions()[1];
    ost << endl;

    Array<shared_ptr<CoefficientFunction>> input = InputCoefficientFunctions();
    for (int i = 0; i < input.Size(); i++)
      input[i] -> PrintReportRec (ost, level+1);
  }
  
  string CoefficientFunction :: GetDescription () const
  {
    return typeid(*this).name();
  }    

  shared_ptr<CoefficientFunction> CoefficientFunction ::
  Diff (const CoefficientFunction * var, shared_ptr<CoefficientFunction> dir) const
  {
    throw Exception(string("Deriv not implemented for CF ")+typeid(*this).name());
  }

  shared_ptr<CoefficientFunction> CoefficientFunctionNoDerivative ::
  Diff (const CoefficientFunction * var, shared_ptr<CoefficientFunction> dir) const
  {
    if (var == this)
      return dir;
    else
      {
        if (Dimension() == 1)
          return make_shared<ConstantCoefficientFunction>(0);
        else
          {
            auto zero1 = make_shared<ConstantCoefficientFunction>(0);
            Array<shared_ptr<CoefficientFunction>> zero_array(Dimension());
            for (auto & z : zero_array)
              z = zero1;
            auto zerovec = MakeVectorialCoefficientFunction(move(zero_array));
            zerovec->SetDimensions(Dimensions());
            return zerovec;
          }
      }
  }
  
  
  void CoefficientFunction :: TraverseTree (const function<void(CoefficientFunction&)> & func)
  {
    func(*this);
  }

  void CoefficientFunction :: 
  Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<double> hvalues) const
  {
    auto values = hvalues.AddSize(ir.Size(), Dimension());
    for (int i = 0; i < ir.Size(); i++)
      Evaluate (ir[i], values.Row(i)); 
  }

  void CoefficientFunction ::   
  Evaluate (const SIMD_BaseMappedIntegrationRule & ir, BareSliceMatrix<SIMD<double>> values) const
  {
    throw ExceptionNOSIMD (string("CF :: simd-Evaluate not implemented for class ") + typeid(*this).name());
  }

  
  /*
  void CoefficientFunction ::   
  Evaluate1 (const SIMD_BaseMappedIntegrationRule & ir, ABareSliceMatrix<double> values) const
  {
    static bool firsttime = true;
    if (firsttime)
      {
        cerr << "Eval1 not implemented for class " << typeid(*this).name() << endl;
        firsttime = false;
      }
    Evaluate (ir, AFlatMatrix<>(Dimension(), ir.IR().GetNIP(), &values.Get(0,0)));
  }
  */

  void CoefficientFunction ::   
  Evaluate (const SIMD_BaseMappedIntegrationRule & ir, BareSliceMatrix<SIMD<Complex>> values) const
  {
    if (IsComplex())
      throw ExceptionNOSIMD (string("CF :: simd-Evaluate (complex) not implemented for class ") + typeid(*this).name());
    else
      {
        size_t nv = ir.Size();
        SliceMatrix<SIMD<double>> overlay(Dimension(), nv, 2*values.Dist(), &values(0,0).real());
        Evaluate (ir, overlay);
        for (size_t i = 0; i < Dimension(); i++)
          for (size_t j = nv; j-- > 0; )
            values(i,j) = overlay(i,j);
      }
  }

  
  void CoefficientFunction :: 
  Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<Complex> values) const
  {
    for (int i = 0; i < ir.Size(); i++)
      Evaluate (ir[i], values.Row(i).AddSize(Dimension())); 
  }

  /*
  void CoefficientFunction ::
  EvaluateSoA (const BaseMappedIntegrationRule & ir, AFlatMatrix<double> values) const
  {
    throw Exception(string ("EvaluateSoA called for ") + typeid(*this).name());
  }
    
  void CoefficientFunction ::
  EvaluateSoA (const BaseMappedIntegrationRule & ir, AFlatMatrix<Complex> values) const
  {
    throw Exception(string ("EvaluateSoAComplex called for ") + typeid(*this).name());
  }
  */

  void CoefficientFunction :: 
  NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                  FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const
  {
    // cout << "CoefficientFunction::NonZeroPattern called, type = " << typeid(*this).name() << endl;
    nonzero = true;
    nonzero_deriv = false;
    nonzero_dderiv = false;
  }
  

  ///
  ConstantCoefficientFunction ::   
  ConstantCoefficientFunction (double aval) 
    : BASE(1, false), val(aval) 
  {
    elementwise_constant = true;
  }

  ConstantCoefficientFunction ::
  ~ConstantCoefficientFunction ()
  { ; }

  void ConstantCoefficientFunction :: PrintReport (ostream & ost) const
  {
    ost << "ConstantCF, val = " << val << endl;
  }

  string ConstantCoefficientFunction :: GetDescription () const 
  {
    return ToString(val);
  }

  
  /*
  virtual string ConsantCoefficientFunction :: GetDescription() const 
  {
    return "Constant "+ToString(val);
  }
  */
  
  void ConstantCoefficientFunction :: Evaluate (const BaseMappedIntegrationRule & ir,
                                                BareSliceMatrix<double> values) const
  {
    values.AddSize(ir.Size(), 1) = val;
  }

  void ConstantCoefficientFunction :: Evaluate (const BaseMappedIntegrationRule & ir,
                                                BareSliceMatrix<Complex> values) const
  {
    values.AddSize(ir.Size(), 1) = val;
  }

  template <typename MIR, typename T, ORDERING ORD>
  void ConstantCoefficientFunction ::
  T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    size_t np = ir.Size();    
    __assume (np > 0);
    for (size_t i = 0; i < np; i++)
      values(0,i) = val;
  }
  
  void ConstantCoefficientFunction :: GenerateCode(Code &code, FlatArray<int> inputs, int index) const
  {
    code.body += Var(index).Declare(code.res_type);
    code.body += Var(index).Assign(Var(val), false);
  }
  
  ///
  ConstantCoefficientFunctionC ::   
  ConstantCoefficientFunctionC (Complex aval) 
    : CoefficientFunction(1, true), val(aval) 
  { ; }

  ConstantCoefficientFunctionC ::
  ~ConstantCoefficientFunctionC ()
  { ; }
  
  double ConstantCoefficientFunctionC :: Evaluate (const BaseMappedIntegrationPoint & ip) const
  {
    throw Exception("no real evaluate for ConstantCF-Complex");
  }

  Complex ConstantCoefficientFunctionC :: EvaluateComplex (const BaseMappedIntegrationPoint & ip) const 
  { 
    return val;
  }
  
  void ConstantCoefficientFunctionC :: Evaluate (const BaseMappedIntegrationPoint & mip, FlatVector<Complex> values) const
  {
    values = val;
  }
  
  void ConstantCoefficientFunctionC :: Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<Complex> values) const
  {
    // values = val;
    for (auto i : Range(ir.Size()))
      values(i, 0) = val;
  }
  
  void ConstantCoefficientFunctionC :: Evaluate (const SIMD_BaseMappedIntegrationRule & ir, BareSliceMatrix<SIMD<Complex>> values) const
  {
    for (auto i : Range(ir.Size()))
      values(0, i) = val;
  }
  
  void ConstantCoefficientFunctionC :: PrintReport (ostream & ost) const
  {
    ost << "ConstantCFC, val = " << val << endl;
  }

  void ConstantCoefficientFunctionC :: GenerateCode(Code &code, FlatArray<int> inputs, int index) const
  {
    code.body += Var(index).Assign(Var(val));
  }


  ///
  ParameterCoefficientFunction ::   
  ParameterCoefficientFunction (double aval) 
    : CoefficientFunctionNoDerivative(1, false), val(aval)
  { ; }

  ParameterCoefficientFunction ::
  ~ParameterCoefficientFunction ()
  { ; }

  void ParameterCoefficientFunction :: PrintReport (ostream & ost) const
  {
    ost << "ParameterCF, val = " << val << endl;
  }

  void ParameterCoefficientFunction :: Evaluate (const BaseMappedIntegrationRule & ir,
                                                 BareSliceMatrix<double> values) const
  {
    values.AddSize(ir.Size(), 1) = val;
  }

  void ParameterCoefficientFunction :: GenerateCode(Code &code, FlatArray<int> inputs, int index) const
  {
    stringstream s;
    s << "*reinterpret_cast<double*>(" << code.AddPointer(&val) << ")";
    code.body += Var(index).Declare(code.res_type);
    code.body += Var(index).Assign(s.str(), false);
  }

  
  
  DomainConstantCoefficientFunction :: 
  DomainConstantCoefficientFunction (const Array<double> & aval)
    : BASE(1, false), val(aval) { ; }
  
  double DomainConstantCoefficientFunction :: Evaluate (const BaseMappedIntegrationPoint & ip) const
  {
    int elind = ip.GetTransformation().GetElementIndex();
    CheckRange (elind);
    return val[elind]; 
  }

  void DomainConstantCoefficientFunction :: Evaluate (const BaseMappedIntegrationRule & ir,
                                                      BareSliceMatrix<double> values) const
  {
    int elind = ir[0].GetTransformation().GetElementIndex();
    CheckRange (elind);    
    values.AddSize(ir.Size(), 1) = val[elind];
  }

  /*
  void DomainConstantCoefficientFunction :: Evaluate (const SIMD_BaseMappedIntegrationRule & ir, BareSliceMatrix<SIMD<double>> values) const
  {
    int elind = ir[0].GetTransformation().GetElementIndex();
    CheckRange (elind);        
    values.AddSize(Dimension(), ir.Size()) = val[elind];
  }
  */
  template <typename MIR, typename T, ORDERING ORD>
  void DomainConstantCoefficientFunction ::
  T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    int elind = ir[0].GetTransformation().GetElementIndex();
    CheckRange (elind);        
    // values.AddSize(Dimension(), ir.Size()) = val[elind];

    size_t np = ir.Size();    
    __assume (np > 0);
    for (size_t i = 0; i < np; i++)
      values(0,i) = val[elind];
  }
  

  void DomainConstantCoefficientFunction :: Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<Complex> values) const
  {
    int elind = ir[0].GetTransformation().GetElementIndex();
    CheckRange (elind);            
    values.AddSize(ir.Size(), 1) = val[elind]; 
  }

  void DomainConstantCoefficientFunction :: GenerateCode(Code &code, FlatArray<int> inputs, int index) const
    {
      code.header += "double tmp_" + ToLiteral(index) + "["+ToLiteral(val.Size())+"] = {";
      for (auto i : Range(val))
      {
        code.header += ToLiteral(val[i]);
        if(i<val.Size()-1)
          code.header += ", ";
      }
      code.header += "};\n";
      code.header += Var(index).Assign("tmp_"+ToLiteral(index) + "[mir.GetTransformation().GetElementIndex()]");
    }


  DomainConstantCoefficientFunction :: 
  ~DomainConstantCoefficientFunction ()
  { ; }

  DomainVariableCoefficientFunction ::
  DomainVariableCoefficientFunction (const EvalFunction & afun)
    : CoefficientFunction(afun.Dimension(), afun.IsResultComplex()), fun(1)
  {
    fun[0] = make_shared<EvalFunction> (afun);
    numarg = 3;
  }

  DomainVariableCoefficientFunction ::
  DomainVariableCoefficientFunction (const EvalFunction & afun,
				     const Array<shared_ptr<CoefficientFunction>> & adepends_on)
    : CoefficientFunction(afun.Dimension(), afun.IsResultComplex()),
      fun(1), depends_on(adepends_on)
  {
    fun[0] = make_shared<EvalFunction> (afun);
    numarg = 3;
    for (int i = 0; i < depends_on.Size(); i++)
      numarg += depends_on[i]->Dimension();
  }


  DomainVariableCoefficientFunction ::
  DomainVariableCoefficientFunction (const Array<shared_ptr<EvalFunction>> & afun)
    : CoefficientFunction(1, false), fun(afun.Size())
  {
    int hdim = -1;
    for (int i = 0; i < fun.Size(); i++)
      if (afun[i])
        {
          fun[i] = afun[i];
          if (fun[i]->IsResultComplex())
            is_complex = true;
          hdim = fun[i]->Dimension();
        }
      else
        fun[i] = nullptr;
    SetDimension (hdim);
    numarg = 3;
  }

  DomainVariableCoefficientFunction ::
  DomainVariableCoefficientFunction (const Array<shared_ptr<EvalFunction>> & afun,
				     const Array<shared_ptr<CoefficientFunction>> & adepends_on)
    : CoefficientFunction(1, false), fun(afun.Size()), depends_on(adepends_on)
  {
    int hdim = -1;
    for (int i = 0; i < fun.Size(); i++)
      if (afun[i])
        {
          fun[i] = afun[i];
          if (fun[i]->IsResultComplex())
            is_complex = true;
          hdim = fun[i]->Dimension();
        }
      else
        fun[i] = nullptr;

    SetDimension (hdim);
    numarg = 3;
    for (int i = 0; i < depends_on.Size(); i++)
      numarg += depends_on[i]->Dimension();
  }


  DomainVariableCoefficientFunction ::
  ~DomainVariableCoefficientFunction ()
  {
    ;
    /*
    for (int i = 0; i < fun.Size(); i++)
      delete fun[i];
    */
  }

  double DomainVariableCoefficientFunction ::
  Evaluate (const BaseMappedIntegrationPoint & ip) const
  {
    Vec<1> result;
    Evaluate (ip, result);
    return result(0);
    /*
      int numarg = max2(3, depends_on.Size());
      VectorMem<10> args(numarg);
      args.Range(0,DIM) = static_cast<const DimMappedIntegrationPoint<DIM>&>(ip).GetPoint();
    
      for (int i = 3; i < depends_on.Size(); i++)
      args(i) = depends_on[i] -> Evaluate (ip);

      int elind = ip.GetTransformation().GetElementIndex();
      if (fun.Size() == 1) elind = 0;
      double val = fun[elind]->Eval (&args(0));
      return val;
    */
  }

  bool DomainVariableCoefficientFunction :: IsComplex() const 
  {
    for (int i = 0; i < fun.Size(); i++)
      if (fun[i]->IsResultComplex()) return true;
    return false;
  }
  
  int DomainVariableCoefficientFunction :: Dimension() const
  { 
    return fun[0]->Dimension(); 
  }


  Complex DomainVariableCoefficientFunction ::
  EvaluateComplex (const BaseMappedIntegrationPoint & ip) const
  {
    Vec<1, Complex> result;
    Evaluate (ip, result);
    return result(0);
    /*
      int elind = ip.GetTransformation().GetElementIndex();
      Vec<DIM, Complex> hp;
      for (int i = 0; i < DIM; i++)
      hp(i) = static_cast<const DimMappedIntegrationPoint<DIM>&>(ip).GetPoint()(i);
      return fun[elind]->Eval (&hp(0));
    */
  }
  
  void DomainVariableCoefficientFunction ::
  Evaluate(const BaseMappedIntegrationPoint & ip,
	   FlatVector<> result) const
  {
    int elind = ip.GetTransformation().GetElementIndex();
    if (fun.Size() == 1) elind = 0;
    
    if (! fun[elind] -> IsComplex ())
      {
	VectorMem<10> args(numarg);
	// args.Range(0,DIM) = static_cast<const DimMappedIntegrationPoint<DIM>&>(ip).GetPoint();
        args.Range(0,ip.Dim()) = ip.GetPoint();
	
	for (int i = 0, an = 3; i < depends_on.Size(); i++)
	  {
	    int dim = depends_on[i]->Dimension();
	    depends_on[i] -> Evaluate (ip, args.Range(an,an+dim));
	    an += dim;
	  }
	fun[elind]->Eval (&args(0), &result(0), result.Size());      
      }
    else
      {
	VectorMem<10, Complex> args(numarg);
	// args.Range(0,DIM) = static_cast<const DimMappedIntegrationPoint<DIM>&>(ip).GetPoint();
        args.Range(0,ip.Dim()) = ip.GetPoint();
	
	for (int i = 0, an = 3; i < depends_on.Size(); i++)
	  {
	    int dim = depends_on[i]->Dimension();
	    depends_on[i] -> Evaluate (ip, args.Range(an,an+dim));
	    an += dim;
	  }
	fun[elind]->Eval (&args(0), &result(0), result.Size());      
      }
  }


  void DomainVariableCoefficientFunction ::
  Evaluate(const BaseMappedIntegrationPoint & ip,
           FlatVector<Complex> result) const
  {
    VectorMem<10,Complex> args(numarg);
    args = -47;
    // args.Range(0,DIM) = static_cast<const DimMappedIntegrationPoint<DIM>&>(ip).GetPoint();
    args.Range(0,ip.Dim()) = ip.GetPoint();
    for (int i = 0, an = 3; i < depends_on.Size(); i++)
      {
        int dim = depends_on[i]->Dimension();
        depends_on[i] -> Evaluate (ip, args.Range(an,an+dim));
        an += dim;
      }
    
    int elind = ip.GetTransformation().GetElementIndex();
    if (fun.Size() == 1) elind = 0;
    fun[elind]->Eval (&args(0), &result(0), result.Size());
  }
  
  
void DomainVariableCoefficientFunction ::
Evaluate (const BaseMappedIntegrationRule & ir, 
	  BareSliceMatrix<double> values) const
{
  if (ir.Size() == 0) return;
  int elind = ir.GetTransformation().GetElementIndex();
  if (fun.Size() == 1) elind = 0;

  if (! fun[elind] -> IsComplex ())
    {
      ArrayMem<double,2000> mem(ir.Size()*numarg);
      FlatMatrix<> args(ir.Size(), numarg, &mem[0]);
      
      int dim = ir[0].Dim();
      switch (dim)
        {
        case 2:
          for (int i = 0; i < ir.Size(); i++)
            args.Row(i).Range(0,2) = ir[i].GetPoint();
          break;
        case 3:
          for (int i = 0; i < ir.Size(); i++)
            args.Row(i).Range(0,3) = ir[i].GetPoint();
          break;
        default:
          for (int i = 0; i < ir.Size(); i++)
            args.Row(i).Range(0,dim) = ir[i].GetPoint();
        }
      

      /*
	args.Row(i).Range(0,DIM) = 
	  static_cast<const DimMappedIntegrationPoint<DIM> & > (ir[i]).GetPoint();
      */
      for (int i = 0, an = 3; i < depends_on.Size(); i++)
	{
	  int dim = depends_on[i]->Dimension();
	  Matrix<> hmat(ir.Size(), dim);
	  depends_on[i] -> Evaluate (ir, hmat);
	  args.Cols(an,an+dim) = hmat;
	  an += dim;
	}
      for (int i = 0; i < ir.Size(); i++)
	fun[elind]->Eval (&args(i,0), &values(i,0), values.Dist());
    }
  else
    {
      Matrix<Complex> args(ir.Size(), numarg);
      for (int i = 0; i < ir.Size(); i++)
	args.Row(i).Range(0,ir[i].Dim()) = ir[i].GetPoint();
      
      for (int i = 0, an = 3; i < depends_on.Size(); i++)
	{
	  int dim = depends_on[i]->Dimension();
	  Matrix<Complex> hmat(ir.Size(), dim);
	  depends_on[i] -> Evaluate (ir, hmat);
	  args.Cols(an,an+dim) = hmat;
	  an += dim;
	}
    
      for (int i = 0; i < ir.Size(); i++)
	fun[elind]->Eval (&args(i,0), &values(i,0), values.Dist());
    }
}

  
void DomainVariableCoefficientFunction :: PrintReport (ostream & ost) const
{
  *testout << "DomainVariableCoefficientFunction, functions are: " << endl;
  for (int i = 0; i < fun.Size(); i++)
    fun[i] -> Print(ost);
}

void DomainVariableCoefficientFunction :: GenerateCode(Code &code, FlatArray<int> inputs, int index) const
{
  code.body += "// DomainVariableCoefficientFunction: not implemented";
}

/*
  template class DomainVariableCoefficientFunction<1>;
  template class DomainVariableCoefficientFunction<2>;
  template class DomainVariableCoefficientFunction<3>;
*/

PolynomialCoefficientFunction::
PolynomialCoefficientFunction(const Array < Array< Array<double>* >* > & polycoeffs_in,
                              const Array < Array<double>* > & polybounds_in)
  : CoefficientFunction(1, false), polycoeffs(polycoeffs_in), polybounds(polybounds_in)
{ ; }

PolynomialCoefficientFunction::
PolynomialCoefficientFunction(const Array < Array<double>* > & polycoeffs_in)
  : CoefficientFunction(1, false)
{
  polycoeffs.SetSize(polycoeffs_in.Size());
  polybounds.SetSize(polycoeffs_in.Size());
  
  for(int i=0; i<polycoeffs_in.Size(); i++)
    {
      polycoeffs[i] = new Array< Array<double>* >(1);
      (*polycoeffs[i])[0] = polycoeffs_in[i];
      polybounds[i] = new Array<double>(0);
    } 
}


PolynomialCoefficientFunction::~PolynomialCoefficientFunction()
{
  for(int i=0; i<polycoeffs.Size(); i++)
    {
      delete polybounds[i];
      for(int j=0; j<polycoeffs[i]->Size(); j++)
	{
	  delete (*polycoeffs[i])[j];
	}
      delete polycoeffs[i];
    }
  polycoeffs.DeleteAll();
  polybounds.DeleteAll();
}
    
  
  
double PolynomialCoefficientFunction::Evaluate (const BaseMappedIntegrationPoint & ip) const
{
  return Evaluate(ip,0);
}



double PolynomialCoefficientFunction::EvalPoly(const double t, const Array<double> & coeffs) const
{
  const int last = coeffs.Size()-1;
    
  double retval = coeffs[last];
  for(int i=last-1; i>=0; i--)
    {
      retval *= t;
      retval += coeffs[i];
    }

  return retval;    
}


double PolynomialCoefficientFunction::EvalPolyDeri(const double t, const Array<double> & coeffs) const
{
  const int last = coeffs.Size()-1;

  double retval = last*coeffs[last];
  for(int i=last-1; i>=1; i--)
    {
      retval *= t;
      retval += i*coeffs[i];
    }  

  return retval;    
}


double PolynomialCoefficientFunction::Evaluate (const BaseMappedIntegrationPoint & ip, const double & t) const
{
  const int elind = ip.GetTransformation().GetElementIndex();
    
  if (elind < 0 || elind >= polycoeffs.Size())
    {
      ostringstream ost;
      ost << "PolynomialCoefficientFunction: Element index "
	  << elind << " out of range 0 - " << polycoeffs.Size()-1 << endl;
      throw Exception (ost.str());
    }
 
  int pos;
  for(pos=0; pos < polybounds[elind]->Size() && t > (*polybounds[elind])[pos]; pos++){}
   
  return EvalPoly(t,*((*(polycoeffs[elind]))[pos]));

    
}


 
double PolynomialCoefficientFunction::EvaluateDeri (const BaseMappedIntegrationPoint & ip, const double & t) const
{
  const int elind = ip.GetTransformation().GetElementIndex();
    
  if (elind < 0 || elind >= polycoeffs.Size())
    {
      ostringstream ost;
      ost << "PolynomialCoefficientFunction: Element index "
	  << elind << " out of range 0 - " << polycoeffs.Size()-1 << endl;
      throw Exception (ost.str());
    }

  int pos;
  for(pos=0; pos < polybounds[elind]->Size() && t > (*polybounds[elind])[pos]; pos++){}

  return EvalPolyDeri(t,*((*(polycoeffs[elind]))[pos]));
}


double PolynomialCoefficientFunction::EvaluateConst () const
{
  return (*(*polycoeffs[0])[0])[0];
}



//////////////////

FileCoefficientFunction :: FileCoefficientFunction ()
  : CoefficientFunction(1, false)
{
  writeips = false;
}

  
FileCoefficientFunction :: FileCoefficientFunction (const string & filename)
  : CoefficientFunction(1, false)  
{
  StartWriteIps(filename);
}

FileCoefficientFunction :: FileCoefficientFunction (const string & aipfilename,
						    const string & ainfofilename,
						    const string & avaluesfilename,
						    const bool loadvalues)
  : CoefficientFunction(1, false)  
{
  ipfilename = aipfilename;
  infofilename = ainfofilename;
  valuesfilename = avaluesfilename;

  if(loadvalues)
    {
      writeips = false;
      LoadValues();
    }
  else
    StartWriteIps();
}
    

  
void FileCoefficientFunction :: EmptyValues(void)
{
  for(int i=0; i<ValuesAtIps.Size(); i++)
    delete ValuesAtIps[i];

  ValuesAtIps.SetSize(0);
}

void FileCoefficientFunction :: Reset(void)
{
  EmptyValues();
}

FileCoefficientFunction :: ~FileCoefficientFunction()
{
  if(writeips)
    StopWriteIps(); 

  EmptyValues();
}


void FileCoefficientFunction :: LoadValues(const string & filename)
{
  cout << "Loading values for coefficient function ..."; cout.flush();

  if(writeips) cerr << "WARNING: CoefficientFunction still writing points to \"" 
		    << ipfilename << "\"" << endl;

  ifstream infile(filename.c_str());
    
  int numels,numips,numentries,eln,ipn;
  double val;

  infile >> numels;
  infile >> numips;
  infile >> numentries;
    
  EmptyValues();
    
  ValuesAtIps.SetSize(numels);
    
  for(int i=0; i<numels; i++)
    {
      ValuesAtIps[i] = new Array<double>(numips);
      *(ValuesAtIps[i]) = 0.;
    }

  for(int i=0; i<numentries; i++)
    {
      infile >> eln;
      infile >> ipn;
      infile >> val;
      (*(ValuesAtIps[eln]))[ipn] = val;
    }

  infile.close();
  cout << "done\n";
}



double FileCoefficientFunction :: Evaluate (const BaseMappedIntegrationPoint & ip) const
{
  const ElementTransformation & eltrans = ip.GetTransformation();
  const int elnum = eltrans.GetElementNr();
  const int ipnum = ip.GetIPNr();

  if(writeips)
    {
      if(elnum > maxelnum) const_cast<int&> (maxelnum) = elnum;
      if(ipnum > maxipnum) const_cast<int&> (maxipnum) = ipnum;
      const_cast<int&> (totalipnum)++;

      Vec<3> point;
      eltrans.CalcPoint(ip.IP(),point);

      const_cast<ofstream&> (outfile) << elnum << " " << ipnum << " " << point << "\n";
    }

  if(elnum < ValuesAtIps.Size())
    {
      return (*(ValuesAtIps[elnum]))[ipnum];
    }

  return 0.;
}

void FileCoefficientFunction :: StartWriteIps(const string & filename)
{
  writeips = true;
  maxelnum = 0;
  maxipnum = 0;
  totalipnum = 0;

  outfile.open(filename.c_str());
  outfile.precision(12);
    
}

void FileCoefficientFunction :: StopWriteIps(const string & infofilename)
{
  writeips = false;

  outfile.close();

    
  cout << "Stopped writing to " << ipfilename << endl;
  cout << "Writing info file to " << infofilename << endl;

  ofstream info(infofilename.c_str());

  info << "numelts " << maxelnum+1 << endl
       << "maxnumips " << maxipnum+1 << endl
       << "totalipnum " << totalipnum << endl;

  info.close();

}




  
class ScaleCoefficientFunction : public T_CoefficientFunction<ScaleCoefficientFunction>
{
  double scal;
  shared_ptr<CoefficientFunction> c1;
  typedef T_CoefficientFunction<ScaleCoefficientFunction> BASE;
public:
  ScaleCoefficientFunction() = default;
  ScaleCoefficientFunction (double ascal, 
                            shared_ptr<CoefficientFunction> ac1)
    : BASE(ac1->Dimension(), ac1->IsComplex()),
      scal(ascal), c1(ac1)
  {
    SetDimensions(c1->Dimensions());
    elementwise_constant = c1->ElementwiseConstant();
  }
  
  void DoArchive (Archive & archive) override
  {
    BASE::DoArchive(archive);
    archive.Shallow(c1) & scal;
  }

  virtual void PrintReport (ostream & ost) const override
  {
    ost << scal << "*(";
    c1->PrintReport(ost);
    ost << ")";
  }

  virtual string GetDescription() const override
  {
    return "Scale "+ToString(scal);
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    TraverseDimensions( c1->Dimensions(), [&](int ind, int i, int j) {
        code.body += Var(index,i,j).Assign(Var(scal) * Var(inputs[0],i,j));
    });
  }

  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1 }); }

  virtual bool DefinedOn (const ElementTransformation & trafo) override
  { return c1->DefinedOn(trafo); }
    
  using BASE::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    return scal * c1->Evaluate(ip);
  }
  virtual Complex EvaluateComplex (const BaseMappedIntegrationPoint & ip) const override
  {
    return scal * c1->EvaluateComplex(ip);
  }
  virtual double EvaluateConst () const override
  {
    return scal * c1->EvaluateConst();
  }
  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<> result) const override
  {
    c1->Evaluate (ip, result);
    result *= scal;
  }
  
  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    c1->Evaluate (ip, result);
    result *= scal;
  }

  virtual void Evaluate (const BaseMappedIntegrationRule & ir,
                         BareSliceMatrix<double> values) const override
  {
    c1->Evaluate (ir, values);
    values.AddSize(ir.Size(), Dimension()) *= scal;
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   BareSliceMatrix<T,ORD> values) const
  {
    c1->Evaluate (ir, values);
    values.AddSize(Dimension(), ir.Size()) *= scal;
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    auto in0 = input[0];
    values.AddSize(Dimension(), ir.Size()) = scal * in0;
  }

  virtual void Evaluate (const BaseMappedIntegrationRule & ir,
                         BareSliceMatrix<Complex> values) const override
  {
    c1->Evaluate (ir, values);
    values.AddSize(ir.Size(), Dimension()) *= scal;
  }
  
  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    c1->NonZeroPattern (ud, nonzero, nonzero_deriv, nonzero_dderiv);
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    values = input[0];
  }

  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    return scal * c1->Diff(var, dir);
  }
  
};


class ScaleCoefficientFunctionC : public CoefficientFunction
{
  Complex scal;
  shared_ptr<CoefficientFunction> c1;
public:
  ScaleCoefficientFunctionC() = default;
  ScaleCoefficientFunctionC (Complex ascal, 
                            shared_ptr<CoefficientFunction> ac1)
    : CoefficientFunction(ac1->Dimension(), true), scal(ascal), c1(ac1)
  {
    SetDimensions (c1->Dimensions());
  }

  void DoArchive(Archive& ar) override
  {
    CoefficientFunction::DoArchive(ar);
    ar.Shallow(c1) & scal;
  }
  
  // virtual bool IsComplex() const { return true; }
  // virtual int Dimension() const { return c1->Dimension(); }

  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1 }); }
  
  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    TraverseDimensions( c1->Dimensions(), [&](int ind, int i, int j) {
        code.body += Var(index,i,j).Assign(Var(scal) * Var(inputs[0],i,j));
    });
  }

  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const  override
  {
    throw Exception ("real Evaluate called for complex ScaleCF");
  }
  
  virtual Complex EvaluateComplex (const BaseMappedIntegrationPoint & ip) const override
  {
    return scal * c1->EvaluateComplex(ip);    
  }
  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    c1->Evaluate (ip, result);
    result *= scal;
  }
  virtual void Evaluate(const BaseMappedIntegrationRule & ir,
                        BareSliceMatrix<Complex> result) const override
  {
    c1->Evaluate (ir, result);
    result.AddSize(ir.Size(), Dimension()) *= scal;
  }

  
  virtual void Evaluate (const SIMD_BaseMappedIntegrationRule & ir,
                         BareSliceMatrix<SIMD<Complex>> values) const override
  {
    c1->Evaluate (ir, values);
    values.AddSize(Dimension(), ir.Size()) *= scal;
  }

  virtual void Evaluate (const BaseMappedIntegrationRule & ir, 
                         BareSliceMatrix<AutoDiffDiff<1,double>> values) const override
  {
    throw Exception ("can't diff complex CF (ScaleCoefficientFunctionC)");
  }
  
  
  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    c1->NonZeroPattern (ud, nonzero, nonzero_deriv, nonzero_dderiv);
  }
};

// ***********************************************************************************

class MultScalVecCoefficientFunction : public T_CoefficientFunction<MultScalVecCoefficientFunction>
{
  shared_ptr<CoefficientFunction> c1;  // scalar
  shared_ptr<CoefficientFunction> c2;  // vector
  typedef T_CoefficientFunction<MultScalVecCoefficientFunction> BASE;
public:
  MultScalVecCoefficientFunction() = default;
  MultScalVecCoefficientFunction (shared_ptr<CoefficientFunction> ac1,
                                  shared_ptr<CoefficientFunction> ac2)
    : BASE(ac2->Dimension(), ac1->IsComplex() || ac2->IsComplex()),
      c1(ac1), c2(ac2)
  {
    SetDimensions (c2->Dimensions());
  }
  
  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    c2->TraverseTree (func);
    func(*this);
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1).Shallow(c2);
  }
  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1, c2 }); }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    TraverseDimensions( c2->Dimensions(), [&](int ind, int i, int j) {
      code.body += Var(index,i,j).Assign( Var(inputs[0]) * Var(inputs[1],i,j) );
    });
  }

  using BASE::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    throw Exception ("double MultScalVecCF::Evaluate called");
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<> result) const override
  {
    Vec<1> v1;
    c1->Evaluate (ip, v1);
    c2->Evaluate (ip, result);
    result *= v1(0);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    Vec<1,Complex> v1;
    c1->Evaluate (ip, v1);
    c2->Evaluate (ip, result);
    result *= v1(0);
  }

  virtual void Evaluate(const BaseMappedIntegrationRule & ir,
                        BareSliceMatrix<Complex> result) const override
  {
    STACK_ARRAY(double, hmem1, 2*ir.Size());
    FlatMatrix<Complex> temp1(ir.Size(), 1, reinterpret_cast<Complex*> (&hmem1[0]));
    
    c1->Evaluate(ir, temp1);
    c2->Evaluate(ir, result);
    for (int i = 0; i < ir.Size(); i++)
      result.Row(i).AddSize(Dimension()) *= temp1(i,0);
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    size_t w = ir.Size();
    __assume (w > 0);
    STACK_ARRAY(T, hmem1, w);
    FlatMatrix<T,ORD> temp1(1, w, &hmem1[0]);
    
    c1->Evaluate (ir, temp1);
    c2->Evaluate (ir, values);

    for (size_t j = 0; j < Dimension(); j++)
      for (size_t i = 0; i < w; i++)
        values(j,i) *= temp1(0,i);
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    auto in0 = input[0];
    auto in1 = input[1];
    size_t dim = Dimension();
    size_t np = ir.Size();
    
    for (size_t j = 0; j < dim; j++)
      for (size_t i = 0; i < np; i++)
        values(j,i) = in0(0,i) * in1(j,i);
  }

  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    return c1->Diff(var,dir)*c2 + c1 * c2->Diff(var,dir);
  }
  
  
  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    int dim = Dimension();
    Vector<bool> v1(1), d1(1), dd1(1);
    Vector<bool> v2(dim), d2(dim), dd2(dim);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    c2->NonZeroPattern (ud, v2, d2, dd2);
    for (auto i : Range(dim))
      {
        nonzero(i) = v1(0) && v2(i);
        nonzero_deriv(i) = (v1(0) && d2(i)) || (d1(0) && v2(i));
        nonzero_dderiv(i) = (v1(0) && dd2(i)) || (d1(0) && d2(i)) || (dd1(0) && v2(i));
      }
  }
  
  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    auto in0 = input[0];
    auto in1 = input[1];
    size_t dim = Dimension();
    
    for (size_t j = 0; j < dim; j++)
      values(j) = in0(0) * in1(j);
  }
};


class MultVecVecCoefficientFunction : public T_CoefficientFunction<MultVecVecCoefficientFunction>
{
  shared_ptr<CoefficientFunction> c1;
  shared_ptr<CoefficientFunction> c2;
  int dim1;
  using BASE = T_CoefficientFunction<MultVecVecCoefficientFunction>;
public:
  MultVecVecCoefficientFunction() = default;
  MultVecVecCoefficientFunction (shared_ptr<CoefficientFunction> ac1,
                                 shared_ptr<CoefficientFunction> ac2)
    : T_CoefficientFunction<MultVecVecCoefficientFunction>(1, ac1->IsComplex() || ac2->IsComplex()), c1(ac1), c2(ac2)
  {
    elementwise_constant = c1->ElementwiseConstant() && c2->ElementwiseConstant();
    dim1 = c1->Dimension();
    if (dim1 != c2->Dimension())
      throw Exception("MultVecVec : dimensions don't fit");
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1).Shallow(c2) & dim1;
  }
  
  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    CodeExpr result;
    TraverseDimensions( c1->Dimensions(), [&](int ind, int i, int j) {
        int i2, j2;
        GetIndex( c2->Dimensions(), ind, i2, j2 );
        result += Var(inputs[0],i,j) * Var(inputs[1],i2,j2);
    });
    code.body += Var(index).Assign(result.S());
  }

  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    c2->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1, c2 }); }  
  
    using T_CoefficientFunction<MultVecVecCoefficientFunction>::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    Vec<1> res;
    Evaluate (ip, res);
    return res(0);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<> result) const override
  {
    STACK_ARRAY(double, hmem1, dim1);
    FlatVector<> v1(dim1, hmem1);
    STACK_ARRAY(double, hmem2, dim1);
    FlatVector<> v2(dim1, hmem2);

    c1->Evaluate (ip, v1);
    c2->Evaluate (ip, v2);
    result(0) = InnerProduct (v1, v2);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    Vector<Complex> v1(dim1), v2(dim1);
    c1->Evaluate (ip, v1);
    c2->Evaluate (ip, v2);
    result(0) = InnerProduct (v1, v2);
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    size_t w = ir.Size();
    __assume (w > 0);

    size_t dim = dim1; // Dimension();
    STACK_ARRAY(T, hmem, 2*dim*w);
    FlatMatrix<T,ORD> temp1(dim, w, &hmem[0]);
    FlatMatrix<T,ORD> temp2(dim, w, &hmem[dim*w]);
    
    c1->Evaluate (ir, temp1);
    c2->Evaluate (ir, temp2);

    for (size_t i = 0; i < w; i++)
      {
        T sum{0.0};
        for (size_t j = 0; j < dim; j++)
          sum += temp1(j,i) * temp2(j,i);
        values(0,i) = sum; 
      }
  }


  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    auto in0 = input[0];
    auto in1 = input[1];
    size_t dim = Dimension();
    size_t np = ir.Size();

    for (size_t i = 0; i < np; i++)
      {
        T sum{0.0};
        for (size_t j = 0; j < dim; j++)
          sum += in0(j,i) * in1(j,i);
        values(0,i) = sum; 
      }    
  }  

  /*
  virtual bool ElementwiseConstant () const override
  { return c1->ElementwiseConstant() && c2->ElementwiseConstant(); }
  */
  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    Vector<bool> v1(dim1), v2(dim1), d1(dim1), d2(dim1), dd1(dim1), dd2(dim1);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    c2->NonZeroPattern (ud, v2, d2, dd2);
    bool nz = false, nzd = false, nzdd = false;
    for (int i = 0; i < dim1; i++)
      {
        if (v1(i) && v2(i)) nz = true;
        if ((v1(i) && d2(i)) || (d1(i) && v2(i))) nzd = true;
        if ((v1(i) && dd2(i)) || (d1(i) && d2(i)) || (dd1(i) && v2(i))) nzdd = true;
      }
    nonzero = nz;
    nonzero_deriv = nzd;
    nonzero_dderiv = nzdd;
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    auto v1 = input[0];
    auto v2 = input[1];
    AutoDiffDiff<1,bool> sum(false);
    for (int i = 0; i < dim1; i++)
      sum += v1(i)*v2(i);
    values(0) = sum;
  }

  
};

template <int DIM>
class T_MultVecVecCoefficientFunction : public T_CoefficientFunction<T_MultVecVecCoefficientFunction<DIM>>
{
  shared_ptr<CoefficientFunction> c1;
  shared_ptr<CoefficientFunction> c2;
  using BASE = T_CoefficientFunction<T_MultVecVecCoefficientFunction<DIM>>;
public:
  T_MultVecVecCoefficientFunction() = default;
  T_MultVecVecCoefficientFunction (shared_ptr<CoefficientFunction> ac1,
                                   shared_ptr<CoefficientFunction> ac2)
    : T_CoefficientFunction<T_MultVecVecCoefficientFunction<DIM>>(1, ac1->IsComplex()||ac2->IsComplex()), c1(ac1), c2(ac2)
  {
    this->elementwise_constant = c1->ElementwiseConstant() && c2->ElementwiseConstant();
    if (DIM != c1->Dimension() || DIM != c2->Dimension())
      throw Exception("T_MultVecVec : dimensions don't fit");
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1).Shallow(c2);
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    CodeExpr result;
    TraverseDimensions( c1->Dimensions(), [&](int ind, int i, int j) {
        int i2, j2;
        GetIndex( c2->Dimensions(), ind, i2, j2 );
        result += Var(inputs[0],i,j) * Var(inputs[1],i2,j2);
    });
    code.body += Var(index).Assign(result.S());
  }

  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    c2->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1, c2 }); }  

  using T_CoefficientFunction<T_MultVecVecCoefficientFunction<DIM>>::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    Vec<1> res;
    Evaluate (ip, res);
    return res(0);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<> result) const override
  {
    Vec<DIM> v1, v2;
    c1->Evaluate (ip, v1);
    c2->Evaluate (ip, v2);
    result(0) = InnerProduct (v1, v2);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    Vec<DIM,Complex> v1, v2;
    c1->Evaluate (ip, v1);
    c2->Evaluate (ip, v2);
    result(0) = InnerProduct (v1, v2);
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    size_t w = ir.Size();
    __assume (w > 0);
    
    STACK_ARRAY(T, hmem, 2*DIM*w);
    FlatMatrix<T,ORD> temp1(DIM, w, &hmem[0]);
    FlatMatrix<T,ORD> temp2(DIM, w, &hmem[DIM*w]);
    
    c1->Evaluate (ir, temp1);
    c2->Evaluate (ir, temp2);

    for (size_t i = 0; i < w; i++)
      {
        T sum{0.0};
        for (size_t j = 0; j < DIM; j++)
          sum += temp1(j,i) * temp2(j,i);
        values(0,i) = sum; 
      }
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    auto in0 = input[0];
    auto in1 = input[1];
    size_t np = ir.Size();

    for (size_t i = 0; i < np; i++)
      {
        T sum{0.0};
        for (size_t j = 0; j < DIM; j++)
          sum += in0(j,i) * in1(j,i);
        values(0,i) = sum; 
      }    
  }  
  
  virtual void Evaluate(const BaseMappedIntegrationRule & ir,
                        BareSliceMatrix<Complex> result) const override
  {
    STACK_ARRAY(double, hmem1, 2*ir.Size()*DIM);
    FlatMatrix<Complex> temp1(ir.Size(), DIM, (Complex*)hmem1);
    STACK_ARRAY(double, hmem2, 2*ir.Size()*DIM);
    FlatMatrix<Complex> temp2(ir.Size(), DIM, (Complex*)hmem2);

    c1->Evaluate(ir, temp1);
    c2->Evaluate(ir, temp2);
    for (int i = 0; i < ir.Size(); i++)
      result(i,0) = InnerProduct(temp1.Row(i), temp2.Row(i));
  }

  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    return InnerProduct(c1->Diff(var,dir),c2) + InnerProduct(c1,c2->Diff(var,dir));
    // return c1->Diff(var,dir)*c2 + c1 * c2->Diff(var,dir);
  }
  

  
  /*
  virtual bool ElementwiseConstant () const override
  { return c1->ElementwiseConstant() && c2->ElementwiseConstant(); }
  */
  
  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    Vector<bool> v1(DIM), v2(DIM), d1(DIM), dd1(DIM), d2(DIM), dd2(DIM);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    c2->NonZeroPattern (ud, v2, d2, dd2);
    // cout << "nonzero, v1 = " << v1 << ", d1 = " << d1 << ", dd1 = " << dd1 << endl;
    // cout << "nonzero, v2 = " << v2 << ", d2 = " << d2 << ", dd2 = " << dd2 << endl;
    bool nz = false, nzd = false, nzdd = false;
    for (int i = 0; i < DIM; i++)
      {
        if (v1(i) && v2(i)) nz = true;
        if ((v1(i) && d2(i)) || (d1(i) && v2(i))) nzd = true;
        if ((v1(i) && dd2(i)) || (d1(i) && d2(i)) || (dd1(i) && v2(i))) nzdd = true;
      }
    // cout << "nz = " << nz << ", nzd = " << nzd << ", nzdd = " << nzdd << endl;
    nonzero = nz;
    nonzero_deriv = nzd;
    nonzero_dderiv = nzdd;
  }


  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    auto v1 = input[0];
    auto v2 = input[1];
    AutoDiffDiff<1,bool> sum(false);
    for (int i = 0; i < DIM; i++)
      sum += v1(i)*v2(i);
    values(0) = sum;
  }
};



class EigCoefficientFunction : public CoefficientFunctionNoDerivative
{
  shared_ptr<CoefficientFunction> cfmat;
  int dim1;
  int vecdim;
  
public:
  EigCoefficientFunction() = default;
  EigCoefficientFunction (shared_ptr<CoefficientFunction> ac1) : CoefficientFunctionNoDerivative(ac1->Dimension() + ac1->Dimensions()[0],false), cfmat(ac1)
  {
    vecdim = cfmat->Dimensions()[0];
    dim1 = cfmat->Dimension();
  }

  void DoArchive(Archive& ar) override
  {
    CoefficientFunctionNoDerivative::DoArchive(ar);
    ar.Shallow(cfmat) & dim1 & vecdim;
  }
  
  using CoefficientFunctionNoDerivative::Evaluate;
  double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    return 0;
  }
  void Evaluate (const BaseMappedIntegrationPoint & ip, FlatVector<> res) const override
  {
    STACK_ARRAY(double,mem, dim1);
    FlatVector<double> vec(dim1, &mem[0]);
    
    cfmat->Evaluate (ip, vec);

    FlatMatrix<double> mat(vecdim,vecdim, &mem[0]);
    FlatVector<double> lami(vecdim, &res[dim1]);
    FlatMatrix<double> eigenvecs(vecdim,vecdim,&res[0]);
    
    CalcEigenSystem(mat,lami,eigenvecs);
  }
};



class NormCoefficientFunction : public T_CoefficientFunction<NormCoefficientFunction>
{
  shared_ptr<CoefficientFunction> c1;
  int dim1;
  typedef double TIN;
  using BASE = T_CoefficientFunction<NormCoefficientFunction>;
public:
  NormCoefficientFunction() = default;
  NormCoefficientFunction (shared_ptr<CoefficientFunction> ac1)
    : T_CoefficientFunction<NormCoefficientFunction> (1, false), c1(ac1)
  {
    dim1 = c1->Dimension();
    elementwise_constant = c1->ElementwiseConstant();
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1) & dim1;
  }
  
  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1 }); }  
  
    using T_CoefficientFunction<NormCoefficientFunction>::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    Vec<1> res;
    Evaluate (ip, res);
    return res(0);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<> result) const override
  {
    VectorMem<10,TIN> v1(dim1);
    c1->Evaluate (ip, v1);
    result(0) = L2Norm(v1);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    Vec<1> res;
    Evaluate (ip, res);
    result(0) = res(0);
  }


  /*
  virtual bool ElementwiseConstant () const override
  { return c1->ElementwiseConstant(); }
  */

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    size_t np = ir.Size();
    size_t dim1 = c1->Dimension();
    STACK_ARRAY(T,mem, np*dim1);
    FlatMatrix<T,ORD> m1(dim1, np, &mem[0]);
    c1->Evaluate (ir, m1);
    
    for (size_t i = 0; i < np; i++)
      {
        T sum{0.0};
        for (size_t j = 0; j < dim1; j++)
          sum += sqr(m1(j,i));
        values(0,i) = sqrt(sum);
      }
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    size_t np = ir.Size();
    auto in = input[0];
    for (size_t i = 0; i < np; i++)
      {
        T sum{0.0};
        for (size_t j = 0; j < dim1; j++)
          sum += sqr(in(j,i));
        values(0,i) = sqrt(sum);
      }
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    auto res = CodeExpr();
    TraverseDimensions( c1->Dimensions(), [&](int ind, int i, int j) {
        res += Var(inputs[0],i,j).Func("L2Norm2");
    });
    code.body += Var(index,0,0).Assign( res.Func("sqrt"));
  }
  
  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    Vector<bool> v1(dim1), d1(dim1), dd1(dim1);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    bool nz = false, nzd = false, nzdd = false;
    for (int i = 0; i < dim1; i++)
      {
        if (v1(i)) nz = true;
        if (d1(i)) nzd = true;
        if (dd1(i)) nzdd = true;
      }
    nonzero = nz;
    nonzero_deriv = nzd;
    nonzero_dderiv = nzd || nzdd;
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    auto v1 = input[0];
    AutoDiffDiff<1,bool> sum(false);
    for (int i = 0; i < dim1; i++)
      sum += v1(i);
    values(0).Value() = sum.Value();
    values(0).DValue(0) = sum.DValue(0);
    values(0).DDValue(0) = sum.DValue(0) || sum.DDValue(0);
  }
};




class NormCoefficientFunctionC : public CoefficientFunction
{
  shared_ptr<CoefficientFunction> c1;
  int dim1;
  typedef Complex TIN;
public:
  NormCoefficientFunctionC() = default;
  NormCoefficientFunctionC (shared_ptr<CoefficientFunction> ac1)
    : CoefficientFunction (1, false), c1(ac1)
  {
    dim1 = c1->Dimension();
    elementwise_constant = c1->ElementwiseConstant(); 
  }

  void DoArchive(Archive& ar) override
  {
    CoefficientFunction::DoArchive(ar);
    ar.Shallow(c1) & dim1;
  }
  
  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1 }); }  
  
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    Vec<1> res;
    Evaluate (ip, res);
    return res(0);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<> result) const override
  {
    VectorMem<10,TIN> v1(dim1);
    c1->Evaluate (ip, v1);
    result(0) = L2Norm(v1);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    Vec<1> res;
    Evaluate (ip, res);
    result(0) = res(0);
  }


  /*
  virtual bool ElementwiseConstant () const override
  { return c1->ElementwiseConstant(); }
  */
  
  virtual void Evaluate(const BaseMappedIntegrationRule & ir,
                        BareSliceMatrix<> result) const override
  {
    STACK_ARRAY(double,hmem,ir.Size()*dim1*sizeof(TIN)/sizeof(double));
    FlatMatrix<TIN> inval(ir.IR().GetNIP(), dim1, reinterpret_cast<TIN*>(&hmem[0]));
    c1->Evaluate (ir, inval);
    for (size_t i = 0; i < ir.Size(); i++)
      result(i,0) = L2Norm(inval.Row(i));
  }


  virtual void Evaluate (const SIMD_BaseMappedIntegrationRule & ir, BareSliceMatrix<SIMD<double>> values) const override
  {
    STACK_ARRAY(SIMD<Complex>,hmem,ir.Size()*dim1);
    FlatMatrix<SIMD<Complex>> inval(dim1, ir.Size(), &hmem[0]);
    c1->Evaluate (ir, inval);
    for (size_t i = 0; i < ir.Size(); i++)
      {
        SIMD<double> sum = 0;
        for (size_t j = 0; j < dim1; j++)
          sum += sqr(inval(j,i).real())+sqr(inval(j,i).imag());
        values(0,i) = sqrt(sum);
      }
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    auto res = CodeExpr();
    TraverseDimensions( c1->Dimensions(), [&](int ind, int i, int j) {
        res += Var(inputs[0],i,j).Func("L2Norm2");
    });
    code.body += Var(index,0,0).Assign( res.Func("sqrt"));
  }

  
  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    Vector<bool> v1(dim1), d1(dim1), dd1(dim1);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    bool nz = false, nzd = false, nzdd = false;
    for (int i = 0; i < dim1; i++)
      {
        if (v1(i)) nz = true;
        if (d1(i)) nzd = true;
        if (dd1(i)) nzdd = true;
      }
    nonzero = nz;
    nonzero_deriv = nzd;
    nonzero_dderiv = nzd || nzdd;
  }
};

  

class MultMatMatCoefficientFunction : public T_CoefficientFunction<MultMatMatCoefficientFunction>
{
  shared_ptr<CoefficientFunction> c1;
  shared_ptr<CoefficientFunction> c2;
  int inner_dim;
  using BASE = T_CoefficientFunction<MultMatMatCoefficientFunction>;
public:
  MultMatMatCoefficientFunction() = default;
  MultMatMatCoefficientFunction (shared_ptr<CoefficientFunction> ac1,
                                 shared_ptr<CoefficientFunction> ac2)
    : T_CoefficientFunction<MultMatMatCoefficientFunction>(1, ac1->IsComplex()||ac2->IsComplex()), c1(ac1), c2(ac2)
  {
    auto dims_c1 = c1 -> Dimensions();
    auto dims_c2 = c2 -> Dimensions();
    if (dims_c1.Size() != 2 || dims_c2.Size() != 2)
      throw Exception("Mult of non-matrices called");
    if (dims_c1[1] != dims_c2[0])
      throw Exception(string("Matrix dimensions don't fit: m1 is ") +
                      ToLiteral(dims_c1[0]) + " x " + ToLiteral(dims_c1[1]) +
                      ", m2 is " + ToLiteral(dims_c2[0]) + " x " + ToLiteral(dims_c2[1]) );
    SetDimensions( ngstd::INT<2> (dims_c1[0], dims_c2[1]) );
    inner_dim = dims_c1[1];
  }
  
  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    c2->TraverseTree (func);
    func(*this);
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1).Shallow(c2) & inner_dim;
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override {
    FlatArray<int> hdims = Dimensions();
      for (int i : Range(hdims[0]))
        for (int j : Range(hdims[1])) {
          CodeExpr s;
          for (int k : Range(inner_dim))
            s += Var(inputs[0], i, k) * Var(inputs[1], k, j);
          code.body += Var(index, i, j).Assign(s);
        }
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1, c2 }); }  


  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    FlatArray<int> hdims = Dimensions();
    Vector<bool> v1(hdims[0]*inner_dim), v2(hdims[1]*inner_dim);
    Vector<bool> d1(hdims[0]*inner_dim), d2(hdims[1]*inner_dim);
    Vector<bool> dd1(hdims[0]*inner_dim), dd2(hdims[1]*inner_dim);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    c2->NonZeroPattern (ud, v2, d2, dd2);
    nonzero = false;
    nonzero_deriv = false;
    nonzero_dderiv = false;
    FlatMatrix<bool> m1(hdims[0], inner_dim, &v1(0));
    FlatMatrix<bool> m2(inner_dim, hdims[1], &v2(0));
    FlatMatrix<bool> md1(hdims[0], inner_dim, &d1(0));
    FlatMatrix<bool> md2(inner_dim, hdims[1], &d2(0));
    FlatMatrix<bool> mdd1(hdims[0], inner_dim, &dd1(0));
    FlatMatrix<bool> mdd2(inner_dim, hdims[1], &dd2(0));

    for (int i = 0; i < hdims[0]; i++)
      for (int j = 0; j < hdims[1]; j++)
        for (int k = 0; k < inner_dim; k++)
          {
            nonzero(i*hdims[1]+j) |= m1(i,k) && m2(k,j);
            nonzero_deriv(i*hdims[1]+j) |= (m1(i,k) && md2(k,j)) || (md1(i,k) && m2(k,j));
            nonzero_dderiv(i*hdims[1]+j) |= (m1(i,k) && mdd2(k,j)) || (md1(i,k) && md2(k,j)) || (mdd1(i,k) && m2(k,j));
          }
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    auto va = input[0];
    auto vb = input[1];

    FlatArray<int> hdims = Dimensions();    
    size_t d1 = hdims[1];

    values = false;
    
    for (size_t j = 0; j < hdims[0]; j++)
      for (size_t k = 0; k < hdims[1]; k++)
        for (size_t l = 0; l < inner_dim; l++)
          values(j*d1+k) += va(j*inner_dim+l) * vb(l*d1+k);
  }

    using T_CoefficientFunction<MultMatMatCoefficientFunction>::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    throw Exception ("MultMatMatCF:: scalar evaluate for matrix called");
  }

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<> result) const override
  {
    FlatArray<int> hdims = Dimensions();
    Vector<> va(hdims[0]*inner_dim);
    Vector<> vb(hdims[1]*inner_dim);
    FlatMatrix<> a(hdims[0], inner_dim, &va[0]);
    FlatMatrix<> b(inner_dim, hdims[1], &vb[0]);
    
    c1->Evaluate (ip, va);
    c2->Evaluate (ip, vb);

    FlatMatrix<> c(hdims[0], hdims[1], &result(0));
    c = a*b;
  }  

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<Complex> result) const override
  {
    FlatArray<int> hdims = Dimensions();
    STACK_ARRAY(double,mema,2*hdims[0]*inner_dim);
    STACK_ARRAY(double,memb,2*hdims[1]*inner_dim);
    FlatVector<Complex> va(hdims[0]*inner_dim, reinterpret_cast<Complex*>(&mema[0]));
    FlatVector<Complex> vb(inner_dim*hdims[1], reinterpret_cast<Complex*>(&memb[0]));
    
    c1->Evaluate (ip, va);
    c2->Evaluate (ip, vb);

    FlatMatrix<Complex> a(hdims[0], inner_dim, &va(0));
    FlatMatrix<Complex> b(inner_dim, hdims[1], &vb(0));

    FlatMatrix<Complex> c(hdims[0], hdims[1], &result(0));
    c = a*b;
    //cout << "MultMatMat: complex not implemented" << endl;
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & mir, BareSliceMatrix<T,ORD> values) const 
  {
    FlatArray<int> hdims = Dimensions();    
    STACK_ARRAY(T, hmem1, mir.Size()*hdims[0]*inner_dim);
    STACK_ARRAY(T, hmem2, mir.Size()*hdims[1]*inner_dim);
    FlatMatrix<T,ORD> va(hdims[0]*inner_dim, mir.Size(), &hmem1[0]);
    FlatMatrix<T,ORD> vb(hdims[1]*inner_dim, mir.Size(), &hmem2[0]);

    c1->Evaluate (mir, va);
    c2->Evaluate (mir, vb);

    values.AddSize(Dimension(),mir.Size()) = T(0.0);

    size_t d1 = hdims[1];
    size_t mir_size = mir.Size();
    for (size_t j = 0; j < hdims[0]; j++)
      for (size_t k = 0; k < hdims[1]; k++)
        for (size_t l = 0; l < inner_dim; l++)
          {
            auto row_a = va.Row(j*inner_dim+l);
            auto row_b = vb.Row(l*d1+k);
            auto row_c = values.Row(j*d1+k);
            for (size_t i = 0; i < mir_size; i++)
              row_c(i) += row_a(i) * row_b(i);
            // row_c = pw_mult (row_a, row_b);
          }
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    auto va = input[0];
    auto vb = input[1];

    FlatArray<int> hdims = Dimensions();    
    size_t d1 = hdims[1];
    size_t np = ir.Size();

    values.AddSize(Dimension(),np) = T(0.0);
    
    for (size_t j = 0; j < hdims[0]; j++)
      for (size_t k = 0; k < hdims[1]; k++)
        for (size_t l = 0; l < inner_dim; l++)
          {
            auto row_a = va.Row(j*inner_dim+l);
            auto row_b = vb.Row(l*d1+k);
            auto row_c = values.Row(j*d1+k);
            for (size_t i = 0; i < np; i++)
              row_c(i) += row_a(i) * row_b(i);
            // row_c = pw_mult (row_a, row_b);
          }
  }

  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (var == this) return dir;
    return c1->Diff(var,dir)*c2 + c1 * c2->Diff(var,dir);
  }
  
};







class MultMatVecCoefficientFunction : public T_CoefficientFunction<MultMatVecCoefficientFunction>
{
  shared_ptr<CoefficientFunction> c1;
  shared_ptr<CoefficientFunction> c2;
  // Array<int> dims;
  int inner_dim;
  using BASE = T_CoefficientFunction<MultMatVecCoefficientFunction>;
public:
  MultMatVecCoefficientFunction() = default;
  MultMatVecCoefficientFunction (shared_ptr<CoefficientFunction> ac1,
                                 shared_ptr<CoefficientFunction> ac2)
    : T_CoefficientFunction(1, ac1->IsComplex()||ac2->IsComplex()), c1(ac1), c2(ac2)
  {
    auto dims_c1 = c1 -> Dimensions();
    auto dims_c2 = c2 -> Dimensions();
    if (dims_c1.Size() != 2 || dims_c2.Size() != 1)
      throw Exception("Not a mat-vec multiplication");
    if (dims_c1[1] != dims_c2[0])
      throw Exception(string ("Matrix dimensions don't fit: mat is ") +
                      ToLiteral(dims_c1[0]) + " x " + ToLiteral(dims_c1[1]) + ", vec is " + ToLiteral(dims_c2[0]));
    SetDimensions (ngstd::INT<1>(dims_c1[0]));
    inner_dim = dims_c1[1];
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1).Shallow(c2) & inner_dim;
  }

  virtual string GetDescription () const override
  { return "Matrix-Vector multiply"; }

  
  // virtual bool IsComplex() const { return c1->IsComplex() || c2->IsComplex(); }
  // virtual int Dimension() const { return dims[0]; }
  // virtual Array<int> Dimensions() const { return Array<int> (dims); } 

  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    c2->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1, c2 }); }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override {
      auto dims = c1->Dimensions();
      for (int i : Range(dims[0])) {
        CodeExpr s;
        for (int j : Range(dims[1]))
            s += Var(inputs[0], i, j) * Var(inputs[1], j);
	code.body += Var(index, i).Assign(s);
      }
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    FlatArray<int> hdims = Dimensions();
    Vector<bool> v1(hdims[0]*inner_dim), v2(inner_dim);
    Vector<bool> d1(hdims[0]*inner_dim), d2(inner_dim);
    Vector<bool> dd1(hdims[0]*inner_dim), dd2(inner_dim);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    c2->NonZeroPattern (ud, v2, d2, dd2);
    nonzero = false;
    nonzero_deriv = false;
    nonzero_dderiv = false;
    FlatMatrix<bool> m1(hdims[0], inner_dim, &v1(0));
    FlatMatrix<bool> md1(hdims[0], inner_dim, &d1(0));
    FlatMatrix<bool> mdd1(hdims[0], inner_dim, &dd1(0));
    for (int i = 0; i < hdims[0]; i++)
      for (int j = 0; j < inner_dim; j++)
        {
          nonzero(i) |= (m1(i,j) && v2(j));
          nonzero_deriv(i) |= ((m1(i,j) && d2(j)) || (md1(i,j) && v2(j)));
          nonzero_dderiv(i) |= ((m1(i,j) && dd2(j)) || (md1(i,j) && d2(j)) || (mdd1(i,j) && v2(j)));
        }
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    auto va = input[0];
    auto vb = input[1];
    
    FlatArray<int> hdims = Dimensions();    
    values = false;
    
    for (size_t i = 0; i < hdims[0]; i++)
      for (size_t j = 0; j < inner_dim; j++)
        values(i) += va(i*inner_dim+j) * vb(j);
  }
    using T_CoefficientFunction<MultMatVecCoefficientFunction>::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    throw Exception ("MultMatVecCF:: scalar evaluate for matrix called");
  }

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<> result) const override
  {
    FlatArray<int> hdims = Dimensions();
    VectorMem<20> va(hdims[0]*inner_dim);
    VectorMem<20> vb(inner_dim);
    FlatMatrix<> a(hdims[0], inner_dim, &va[0]);

    c1->Evaluate (ip, va);
    c2->Evaluate (ip, vb);

    result = a * vb;
  }  

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<Complex> result) const override
  {
    FlatArray<int> hdims = Dimensions();
    STACK_ARRAY(double,mema,2*hdims[0]*inner_dim);
    STACK_ARRAY(double,memb,2*inner_dim);
    FlatVector<Complex> va(hdims[0]*inner_dim,reinterpret_cast<Complex*>(&mema[0]));
    FlatVector<Complex> vb(inner_dim,reinterpret_cast<Complex*>(&memb[0]));
    FlatMatrix<Complex> a(hdims[0], inner_dim, &va(0));

    c1->Evaluate (ip, va);
    c2->Evaluate (ip, vb);

    result = a * vb;
    //cout << "MultMatMat: complex not implemented" << endl;
  }  


  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    FlatArray<int> hdims = Dimensions();    
    STACK_ARRAY(T, hmem1, ir.Size()*hdims[0]*inner_dim);
    STACK_ARRAY(T, hmem2, ir.Size()*inner_dim);
    FlatMatrix<T,ORD> temp1(hdims[0]*inner_dim, ir.Size(), &hmem1[0]);
    FlatMatrix<T,ORD> temp2(inner_dim, ir.Size(), &hmem2[0]);
    c1->Evaluate (ir, temp1);
    c2->Evaluate (ir, temp2);
    values.AddSize(Dimension(),ir.Size()) = T(0.0);
    for (size_t i = 0; i < hdims[0]; i++)
      for (size_t j = 0; j < inner_dim; j++)
        for (size_t k = 0; k < ir.Size(); k++)
          values(i,k) += temp1(i*inner_dim+j, k) * temp2(j,k);
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    auto va = input[0];
    auto vb = input[1];
    
    FlatArray<int> hdims = Dimensions();    
    values.AddSize(Dimension(),ir.Size()) = T(0.0);
    
    for (size_t i = 0; i < hdims[0]; i++)
      for (size_t j = 0; j < inner_dim; j++)
        for (size_t k = 0; k < ir.Size(); k++)
          values(i,k) += va(i*inner_dim+j, k) * vb(j,k);
  }


  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    return c1->Diff(var,dir)*c2 + c1 * c2->Diff(var,dir);
  }
  
  
};



  
class TransposeCoefficientFunction : public T_CoefficientFunction<TransposeCoefficientFunction>
{
  shared_ptr<CoefficientFunction> c1;
  using BASE = T_CoefficientFunction<TransposeCoefficientFunction>;
public:
  TransposeCoefficientFunction() = default;
  TransposeCoefficientFunction (shared_ptr<CoefficientFunction> ac1)
    : T_CoefficientFunction<TransposeCoefficientFunction>(1, ac1->IsComplex()), c1(ac1)
  {
    auto dims_c1 = c1 -> Dimensions();
    if (dims_c1.Size() != 2)
      throw Exception("Transpose of non-matrix called");

    SetDimensions (ngstd::INT<2> (dims_c1[1], dims_c1[0]) );
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1);
  }

  virtual string GetDescription () const override
  { return "Matrix transpose"; }
  
  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override {
      FlatArray<int> hdims = Dimensions();        
      for (int i : Range(hdims[0]))
        for (int j : Range(hdims[1]))
          code.body += Var(index,i,j).Assign( Var(inputs[0],j,i) );
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1 } ); }  

  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    FlatArray<int> hdims = Dimensions();    
    Vector<bool> v1(hdims[0]*hdims[1]), d1(hdims[0]*hdims[1]), dd1(hdims[0]*hdims[1]);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    {
      FlatMatrix<bool> m1(hdims[1], hdims[0], &v1(0));
      FlatMatrix<bool> m2(hdims[0], hdims[1], &nonzero(0));
      m2 = Trans(m1);
    }
    {
      FlatMatrix<bool> m1(hdims[1], hdims[0], &d1(0));
      FlatMatrix<bool> m2(hdims[0], hdims[1], &nonzero_deriv(0));
      m2 = Trans(m1);
    }
    {
      FlatMatrix<bool> m1(hdims[1], hdims[0], &dd1(0));
      FlatMatrix<bool> m2(hdims[0], hdims[1], &nonzero_dderiv(0));
      m2 = Trans(m1);
    }
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    FlatArray<int> hdims = Dimensions();    
    auto in0 = input[0];
    for (size_t j = 0; j < hdims[0]; j++)
      for (size_t k = 0; k < hdims[1]; k++)
        values(j*hdims[1]+k) = in0(k*hdims[0]+j);
  }
    using T_CoefficientFunction<TransposeCoefficientFunction>::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    throw Exception ("TransposeCF:: scalar evaluate for matrix called");
  }

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<> result) const override
  {
    FlatArray<int> hdims = Dimensions();        
    VectorMem<20> input(result.Size());
    c1->Evaluate (ip, input);    
    FlatMatrix<> reshape1(hdims[1], hdims[0], &input(0));  // source matrix format
    FlatMatrix<> reshape2(hdims[0], hdims[1], &result(0));  // range matrix format
    reshape2 = Trans(reshape1);
    
    /*
    c1->Evaluate (ip, result);
    static Timer t("Transpose - evaluate");
    RegionTimer reg(t);
    FlatMatrix<> reshape(dims[1], dims[0], &result(0));  // source matrix format
    Matrix<> tmp = Trans(reshape);
    FlatMatrix<> reshape2(dims[0], dims[1], &result(0));  // range matrix format
    reshape2 = tmp;
    */
  }  

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<Complex> result) const override
  {
    FlatArray<int> hdims = Dimensions();        
    STACK_ARRAY(double,meminput,2*hdims[0]*hdims[1]);
    FlatVector<Complex> input(hdims[0]*hdims[1],reinterpret_cast<Complex*>(&meminput[0]));
    c1->Evaluate (ip, input);    
    FlatMatrix<Complex> reshape1(hdims[1], hdims[0], &input(0));  // source matrix format
    FlatMatrix<Complex> reshape2(hdims[0], hdims[1], &result(0));  // range matrix format
    reshape2 = Trans(reshape1);
    //cout << "Transpose: complex not implemented" << endl;
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & mir,
                   BareSliceMatrix<T,ORD> result) const
  {
    FlatArray<int> hdims = Dimensions();    
    c1->Evaluate (mir, result);
    STACK_ARRAY(T, hmem, hdims[0]*hdims[1]);
    FlatMatrix<T,ORD> tmp (hdims[0], hdims[1], &hmem[0]);

    for (size_t i = 0; i < mir.Size(); i++)
      {
        for (int j = 0; j < hdims[0]; j++)
          for (int k = 0; k < hdims[1]; k++)
            tmp(j,k) = result(k*hdims[0]+j, i);
        for (int j = 0; j < hdims[0]; j++)
          for (int k = 0; k < hdims[1]; k++)
            result(j*hdims[1]+k, i) = tmp(j,k);
      }
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    FlatArray<int> hdims = Dimensions();
    size_t np = ir.Size();
    
    auto in0 = input[0];
    for (size_t j = 0; j < hdims[0]; j++)
      for (size_t k = 0; k < hdims[1]; k++)
        for (size_t i = 0; i < np; i++)
          values(j*hdims[1]+k, i) = in0(k*hdims[0]+j, i);
  }

  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    return TransposeCF (c1->Diff(var, dir));
  }  
};



template <int D>
class InverseCoefficientFunction : public T_CoefficientFunction<InverseCoefficientFunction<D>>
{
  shared_ptr<CoefficientFunction> c1;
  using BASE = T_CoefficientFunction<InverseCoefficientFunction<D>>;
public:
  InverseCoefficientFunction() = default;
  InverseCoefficientFunction (shared_ptr<CoefficientFunction> ac1)
    : T_CoefficientFunction<InverseCoefficientFunction>(D*D, ac1->IsComplex()), c1(ac1)
  {
    this->SetDimensions (ngstd::INT<2> (D,D));
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1);
  }
  
  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override {
    auto mat_type = "Mat<"+ToString(D)+","+ToString(D)+","+code.res_type+">";
    auto mat_var = Var("mat", index);
    auto inv_var = Var("inv", index);
    code.body += mat_var.Declare(mat_type);
    code.body += inv_var.Declare(mat_type);
    for (int j = 0; j < D; j++)
      for (int k = 0; k < D; k++)
        code.body += mat_var(j,k).Assign(Var(inputs[0], j, k), false);

    code.body += inv_var.Assign(mat_var.Func("Inv"), false);

    for (int j = 0; j < D; j++)
      for (int k = 0; k < D; k++)
        code.body += Var(index, j, k).Assign(inv_var(j,k));
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1 } ); }  

  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    nonzero = true;
    nonzero_deriv = true;
    nonzero_dderiv = true;
    /*
    FlatArray<int> hdims = Dimensions();    
    Vector<bool> v1(D*D), d1(D*D), dd1(D*D);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    {
      FlatMatrix<bool> m1(hdims[1], hdims[0], &v1(0));
      FlatMatrix<bool> m2(hdims[0], hdims[1], &nonzero(0));
      m2 = Trans(m1);
    }
    {
      FlatMatrix<bool> m1(hdims[1], hdims[0], &d1(0));
      FlatMatrix<bool> m2(hdims[0], hdims[1], &nonzero_deriv(0));
      m2 = Trans(m1);
    }
    {
      FlatMatrix<bool> m1(hdims[1], hdims[0], &dd1(0));
      FlatMatrix<bool> m2(hdims[0], hdims[1], &nonzero_dderiv(0));
      m2 = Trans(m1);
    }
    */
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    AutoDiffDiff<1,bool> add(true);
    add.DValue(0) = true;
    add.DDValue(0,0) = true;
    values = add;
    /*
    FlatArray<int> hdims = Dimensions();    
    auto in0 = input[0];
    for (size_t j = 0; j < hdims[0]; j++)
      for (size_t k = 0; k < hdims[1]; k++)
        values(j*hdims[1]+k) = in0(k*hdims[0]+j);
    */
  }
  using T_CoefficientFunction<InverseCoefficientFunction<D>>::Evaluate;

  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    throw Exception ("InverseCF:: scalar evaluate for matrix called");
  }

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<> result) const override
  {
    Mat<D,D> mat;
    c1->Evaluate (ip, FlatVector<> (D*D, &mat(0,0)));
    Mat<D,D> inv = Inv(mat);
    result = FlatVector<> (D*D, &inv(0,0));
  }  

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<Complex> result) const override
  {
    Mat<D,D,Complex> mat;
    c1->Evaluate (ip, FlatVector<Complex> (D*D, &mat(0,0)));
    Mat<D,D, Complex> inv = Inv(mat);
    result = FlatVector<Complex> (D*D, &inv(0,0));
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & mir,
                   BareSliceMatrix<T,ORD> result) const
  {
    c1->Evaluate (mir, result);
    for (size_t i = 0; i < mir.Size(); i++)
      {
        Mat<D,D,T> hm;
        for (int j = 0; j < D; j++)
          for (int k = 0; k < D; k++)
            hm(j,k) = result(j*D+k, i);
        hm = Inv(hm);
        for (int j = 0; j < D; j++)
          for (int k = 0; k < D; k++)
            result(j*D+k, i) = hm(j,k);
      }
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    size_t np = ir.Size();
    auto in0 = input[0];

    for (size_t i = 0; i < np; i++)
      {
        Mat<D,D,T> hm;
        for (int j = 0; j < D; j++)
          for (int k = 0; k < D; k++)
            hm(j,k) = in0(j*D+k, i);
        hm = Inv(hm);
        for (int j = 0; j < D; j++)
          for (int k = 0; k < D; k++)
            values(j*D+k, i) = hm(j,k);
      }
  }

  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    return (-1)*InverseCF(c1) * c1->Diff(var,dir) * InverseCF(c1);
  }  
};





template <int D>
class DeterminantCoefficientFunction : public T_CoefficientFunction<DeterminantCoefficientFunction<D>>
{
  shared_ptr<CoefficientFunction> c1;
  using BASE = T_CoefficientFunction<DeterminantCoefficientFunction<D>>;
public:
  DeterminantCoefficientFunction() = default;
  DeterminantCoefficientFunction (shared_ptr<CoefficientFunction> ac1)
    : T_CoefficientFunction<DeterminantCoefficientFunction>(1, ac1->IsComplex()), c1(ac1)
  {
    ;
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1);
  }
  
  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override {
    auto mat_type = "Mat<"+ToString(D)+","+ToString(D)+","+code.res_type+">";
    auto mat_var = Var("mat", index);
    code.body += mat_var.Declare(mat_type);
    for (int j = 0; j < D; j++)
      for (int k = 0; k < D; k++)
        code.body += mat_var(j,k).Assign(Var(inputs[0], j, k), false);

    code.body += Var(index).Assign(mat_var.Func("Det"));
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1 } ); }  

  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    nonzero = true;
    nonzero_deriv = true;
    nonzero_dderiv = true;
    /*
    FlatArray<int> hdims = Dimensions();    
    Vector<bool> v1(D*D), d1(D*D), dd1(D*D);
    c1->NonZeroPattern (ud, v1, d1, dd1);
    {
      FlatMatrix<bool> m1(hdims[1], hdims[0], &v1(0));
      FlatMatrix<bool> m2(hdims[0], hdims[1], &nonzero(0));
      m2 = Trans(m1);
    }
    {
      FlatMatrix<bool> m1(hdims[1], hdims[0], &d1(0));
      FlatMatrix<bool> m2(hdims[0], hdims[1], &nonzero_deriv(0));
      m2 = Trans(m1);
    }
    {
      FlatMatrix<bool> m1(hdims[1], hdims[0], &dd1(0));
      FlatMatrix<bool> m2(hdims[0], hdims[1], &nonzero_dderiv(0));
      m2 = Trans(m1);
    }
    */
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    AutoDiffDiff<1,bool> add(true);
    add.DValue(0) = true;
    add.DDValue(0,0) = true;
    values = add;
    /*
    FlatArray<int> hdims = Dimensions();    
    auto in0 = input[0];
    for (size_t j = 0; j < hdims[0]; j++)
      for (size_t k = 0; k < hdims[1]; k++)
        values(j*hdims[1]+k) = in0(k*hdims[0]+j);
    */
  }
  using T_CoefficientFunction<DeterminantCoefficientFunction<D>>::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    throw Exception ("DeterminantCF:: scalar evaluate for matrix called");
  }

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<> result) const override
  {
    Mat<D,D> mat;
    c1->Evaluate (ip, FlatVector<> (D*D, &mat(0,0)));
    result(0) = Det(mat);
  }  

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<Complex> result) const override
  {
    Mat<D,D,Complex> mat;
    c1->Evaluate (ip, FlatVector<Complex> (D*D, &mat(0,0)));
    result(0) = Det(mat);
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & mir,
                   BareSliceMatrix<T,ORD> result) const
  {
    STACK_ARRAY(T, hmem, mir.Size()*D*D);
    FlatMatrix<T,ORD> hv(D*D, mir.Size(), &hmem[0]);
    c1->Evaluate (mir, hv);
    
    for (size_t i = 0; i < mir.Size(); i++)
      {
        Mat<D,D,T> hm;
        for (int j = 0; j < D; j++)
          for (int k = 0; k < D; k++)
            hm(j,k) = hv(j*D+k, i);
        result(0,i) = Det(hm);
      }
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    size_t np = ir.Size();
    auto in0 = input[0];

    for (size_t i = 0; i < np; i++)
      {
        Mat<D,D,T> hm;
        for (int j = 0; j < D; j++)
          for (int k = 0; k < D; k++)
            hm(j,k) = in0(j*D+k, i);
        values(0,i) = Det(hm);        
      }
  }

  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    return DeterminantCF(c1) * InnerProduct( TransposeCF(InverseCF(c1)), c1->Diff(var,dir) );
  }  
};










class SymmetricCoefficientFunction : public T_CoefficientFunction<SymmetricCoefficientFunction>
{
  shared_ptr<CoefficientFunction> c1;
  using BASE = T_CoefficientFunction<SymmetricCoefficientFunction>;
public:
  SymmetricCoefficientFunction() = default;
  SymmetricCoefficientFunction (shared_ptr<CoefficientFunction> ac1)
    : T_CoefficientFunction<SymmetricCoefficientFunction>(1, ac1->IsComplex()), c1(ac1)
  {
    auto dims_c1 = c1 -> Dimensions();
    if (dims_c1.Size() != 2)
      throw Exception("Sym of non-matrix called");
    if (dims_c1[0] != dims_c1[1])
      throw Exception("Sym of non-symmetric matrix called");
    
    SetDimensions (ngstd::INT<2> (dims_c1[0], dims_c1[0]) );
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1);
  }
  
  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override {
      FlatArray<int> hdims = Dimensions();        
      for (int i : Range(hdims[0]))
        for (int j : Range(hdims[1]))
          code.body += Var(index,i,j).Assign("0.5*("+Var(inputs[0],i,j).S()+"+"+Var(inputs[0],j,i).S()+")");
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1 } ); }  

  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    cout << "nonzero, rec" << endl;
    int hd = Dimensions()[0];    
    c1->NonZeroPattern (ud, nonzero, nonzero_deriv, nonzero_dderiv);
    cout << "non-zero input " << nonzero << endl;
    for (int i = 0; i < hd; i++)
      for (int j = 0; j < hd; j++)
        {
          int ii = i*hd+j;
          int jj = j*hd+i;
          nonzero(ii) |= nonzero(jj);
          nonzero_deriv(ii) |= nonzero_deriv(jj);
          nonzero_dderiv(ii) |= nonzero_dderiv(jj);
        }
    cout << "non-zero result " << nonzero << endl;    
  }

  
  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    int hd = Dimensions()[0];    
    auto in0 = input[0];
    for (int i = 0; i < hd; i++)
      for (int j = 0; j < hd; j++)
        {
          int ii = i*hd+j;
          int jj = j*hd+i;
          values(ii) = in0(ii)+in0(jj);   // logical or 
        }
  }
  using T_CoefficientFunction<SymmetricCoefficientFunction>::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    throw Exception ("TransposeCF:: scalar evaluate for matrix called");
  }

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<> result) const override
  {
    FlatArray<int> hdims = Dimensions();        
    VectorMem<20> input(result.Size());
    c1->Evaluate (ip, input);    
    FlatMatrix<> reshape1(hdims[1], hdims[0], &input(0));  // source matrix format
    FlatMatrix<> reshape2(hdims[0], hdims[1], &result(0));  // range matrix format
    reshape2 = 0.5 * (reshape1+Trans(reshape1));
  }  

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<Complex> result) const override
  {
    FlatArray<int> hdims = Dimensions();        
    STACK_ARRAY(double,meminput,2*hdims[0]*hdims[1]);
    FlatVector<Complex> input(hdims[0]*hdims[1],reinterpret_cast<Complex*>(&meminput[0]));
    c1->Evaluate (ip, input);    
    FlatMatrix<Complex> reshape1(hdims[1], hdims[0], &input(0));  // source matrix format
    FlatMatrix<Complex> reshape2(hdims[0], hdims[1], &result(0));  // range matrix format
    reshape2 = 0.5 * (reshape1+Trans(reshape1));
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & mir,
                   BareSliceMatrix<T,ORD> result) const
  {
    int hd = Dimensions()[0];
    c1->Evaluate (mir, result);
    STACK_ARRAY(T, hmem, hd*hd);
    FlatMatrix<T,ORD> tmp (hd, hd, &hmem[0]);

    for (size_t i = 0; i < mir.Size(); i++)
      {
        for (int j = 0; j < hd; j++)
          for (int k = 0; k < hd; k++)
            tmp(j,k) = result(k*hd+j, i);
        for (int j = 0; j < hd; j++)
          for (int k = 0; k < hd; k++)
            result(j*hd+k, i) = 0.5*(tmp(j,k)+tmp(k,j));
      }
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    int hd = Dimensions()[0];
    size_t np = ir.Size();
    
    auto in0 = input[0];
    for (size_t j = 0; j < hd; j++)
      for (size_t k = 0; k < hd; k++)
        for (size_t i = 0; i < np; i++)
          values(j*hd+k, i) = 0.5 * (in0(k*hd+j, i)+in0(j*hd+k, i));
  }
};




  

  
  // ///////////////////////////// operators  /////////////////////////

  struct GenericPlus {
    template <typename T> T operator() (T x, T y) const { return x+y; }
    void DoArchive(Archive& ar){}
  };
  struct GenericMinus {
    template <typename T> T operator() (T x, T y) const { return x-y; }
    void DoArchive(Archive& ar){}
  };
  struct GenericMult {
    template <typename T> T operator() (T x, T y) const { return x*y; }
    void DoArchive(Archive& ar){}
  };
  struct GenericDiv {
    template <typename T> T operator() (T x, T y) const { return x/y; }
    void DoArchive(Archive& ar){}
  };
  GenericPlus gen_plus;
  GenericMinus gen_minus;
  GenericMult gen_mult;
  GenericDiv gen_div;

template <> 
shared_ptr<CoefficientFunction>
cl_BinaryOpCF<GenericPlus>::Diff(const CoefficientFunction * var,
                                   shared_ptr<CoefficientFunction> dir) const
{
  if (var == this) return dir;
  return c1->Diff(var,dir) + c2->Diff(var,dir);
}

shared_ptr<CoefficientFunction> operator+ (shared_ptr<CoefficientFunction> c1, shared_ptr<CoefficientFunction> c2)
{
  return BinaryOpCF (c1, c2, gen_plus, "+");
}


template <> 
shared_ptr<CoefficientFunction>
cl_BinaryOpCF<GenericMinus>::Diff(const CoefficientFunction * var,
                                    shared_ptr<CoefficientFunction> dir) const
{
  if (var == this) return dir;      
  return c1->Diff(var,dir) - c2->Diff(var,dir);
}

shared_ptr<CoefficientFunction> operator- (shared_ptr<CoefficientFunction> c1, shared_ptr<CoefficientFunction> c2)
{
  return BinaryOpCF (c1, c2, gen_minus, "-");
}

template <> 
shared_ptr<CoefficientFunction>
cl_BinaryOpCF<GenericMult>::Diff(const CoefficientFunction * var,
                                   shared_ptr<CoefficientFunction> dir) const
{
  if (var == this) return dir;    
  return c1->Diff(var,dir)*c2 + c1*c2->Diff(var,dir);
}

template <> 
shared_ptr<CoefficientFunction>
cl_BinaryOpCF<GenericDiv>::Diff(const CoefficientFunction * var,
                                   shared_ptr<CoefficientFunction> dir) const
{
  if (var == this) return dir;
  return (c1->Diff(var,dir)*c2 - c1*c2->Diff(var,dir)) / (c2*c2);
}


shared_ptr<CoefficientFunction> operator* (shared_ptr<CoefficientFunction> c1, shared_ptr<CoefficientFunction> c2)
  {
    if (c1->Dimensions().Size() == 2 && c2->Dimensions().Size() == 2)
      return make_shared<MultMatMatCoefficientFunction> (c1, c2);
    if (c1->Dimensions().Size() == 2 && c2->Dimensions().Size() == 1)
      return make_shared<MultMatVecCoefficientFunction> (c1, c2);
    if (c1->Dimension() > 1 && c2->Dimension() > 1)
      {
        switch (c1->Dimension())
          {
          case 2:
            return make_shared<T_MultVecVecCoefficientFunction<2>> (c1, c2);
          case 3:
            return make_shared<T_MultVecVecCoefficientFunction<3>> (c1, c2);
          case 4:
            return make_shared<T_MultVecVecCoefficientFunction<4>> (c1, c2);
          case 5:
            return make_shared<T_MultVecVecCoefficientFunction<5>> (c1, c2);
          default:
            return make_shared<MultVecVecCoefficientFunction> (c1, c2);
          }
      }
    if (c1->Dimension() == 1 && c2->Dimension() > 1)
      return make_shared<MultScalVecCoefficientFunction> (c1, c2);
    if (c1->Dimension() > 1 && c2->Dimension() == 1)
      return make_shared<MultScalVecCoefficientFunction> (c2, c1);
    
    return BinaryOpCF (c1, c2, gen_mult,"*");
  }

  shared_ptr<CoefficientFunction> operator* (double v1, shared_ptr<CoefficientFunction> c2)
  {
    return make_shared<ScaleCoefficientFunction> (v1, c2); 
  }
  
  shared_ptr<CoefficientFunction> operator* (Complex v1, shared_ptr<CoefficientFunction> c2)
  {
    return make_shared<ScaleCoefficientFunctionC> (v1, c2); 
  }

  shared_ptr<CoefficientFunction> InnerProduct (shared_ptr<CoefficientFunction> c1,
                                                shared_ptr<CoefficientFunction> c2)
  {
    switch (c1->Dimension())
      {
      case 1:
        return make_shared<T_MultVecVecCoefficientFunction<1>> (c1, c2);
      case 2:
        return make_shared<T_MultVecVecCoefficientFunction<2>> (c1, c2);
      case 3:
        return make_shared<T_MultVecVecCoefficientFunction<3>> (c1, c2);
      case 4:
        return make_shared<T_MultVecVecCoefficientFunction<4>> (c1, c2);
      case 5:
        return make_shared<T_MultVecVecCoefficientFunction<5>> (c1, c2);
      case 6:
        return make_shared<T_MultVecVecCoefficientFunction<6>> (c1, c2);
      case 8:
        return make_shared<T_MultVecVecCoefficientFunction<8>> (c1, c2);
      case 9:
        return make_shared<T_MultVecVecCoefficientFunction<9>> (c1, c2);
      default:
        return make_shared<MultVecVecCoefficientFunction> (c1, c2);
      }
    
    // return make_shared<MultVecVecCoefficientFunction> (c1, c2);
  }

  shared_ptr<CoefficientFunction> TransposeCF (shared_ptr<CoefficientFunction> coef)
  {
    return make_shared<TransposeCoefficientFunction> (coef);
  }

  shared_ptr<CoefficientFunction> InverseCF (shared_ptr<CoefficientFunction> coef)
  {
    auto dims = coef->Dimensions();
    if (dims.Size() != 2) throw Exception("Inverse of non-matrix");
    if (dims[0] != dims[1]) throw Exception("Inverse of non-quadratic matrix");
    switch (dims[0])
      {
      case 1: return make_shared<InverseCoefficientFunction<1>> (coef);
      case 2: return make_shared<InverseCoefficientFunction<2>> (coef);
      case 3: return make_shared<InverseCoefficientFunction<3>> (coef);
      default:
        throw Exception("Inverse of matrix of size "+ToString(dims[0]) + " not available");
      }
  }

  shared_ptr<CoefficientFunction> DeterminantCF (shared_ptr<CoefficientFunction> coef)
  {
    auto dims = coef->Dimensions();
    if (dims.Size() != 2) throw Exception("Inverse of non-matrix");
    if (dims[0] != dims[1]) throw Exception("Inverse of non-quadratic matrix");
    switch (dims[0])
      {
      case 1: return make_shared<DeterminantCoefficientFunction<1>> (coef);
      case 2: return make_shared<DeterminantCoefficientFunction<2>> (coef);
      case 3: return make_shared<DeterminantCoefficientFunction<3>> (coef);
      default:
        throw Exception("Determinant of matrix of size "+ToString(dims[0]) + " not available");
      }
  }


  shared_ptr<CoefficientFunction> SymmetricCF (shared_ptr<CoefficientFunction> coef)
  {
    return make_shared<SymmetricCoefficientFunction> (coef);
  }

  shared_ptr<CoefficientFunction> NormCF (shared_ptr<CoefficientFunction> coef)
  {
    if (coef->IsComplex())
      return make_shared<NormCoefficientFunctionC> (coef);
    else
      return make_shared<NormCoefficientFunction> (coef);
  }

  shared_ptr<CoefficientFunction> EigCF (shared_ptr<CoefficientFunction> coef)
  {
    return make_shared<EigCoefficientFunction> (coef);
  }
  
  shared_ptr<CoefficientFunction> operator/ (shared_ptr<CoefficientFunction> c1, shared_ptr<CoefficientFunction> c2)
  {
    return BinaryOpCF (c1, c2, gen_div, "/");
  }

  
class ComponentCoefficientFunction : public T_CoefficientFunction<ComponentCoefficientFunction>
{
  shared_ptr<CoefficientFunction> c1;
  int dim1;
  int comp;
  typedef T_CoefficientFunction<ComponentCoefficientFunction> BASE;
public:
  ComponentCoefficientFunction() = default;
  ComponentCoefficientFunction (shared_ptr<CoefficientFunction> ac1,
                                int acomp)
    : BASE(1, ac1->IsComplex()), c1(ac1), comp(acomp)
  {
    dim1 = c1->Dimension();
    elementwise_constant = c1->ElementwiseConstant();
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1) & dim1 & comp;
  }
  
  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    auto dims = c1->Dimensions();
    int i,j;
    GetIndex(dims, comp, i, j);
    code.body += Var(index).Assign( Var(inputs[0], i, j ));
  }

  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  { return Array<shared_ptr<CoefficientFunction>>({ c1 }); }

  using BASE::Evaluate;
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    VectorMem<20> v1(c1->Dimension());
    c1->Evaluate (ip, v1);
    return v1(comp);
  }

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<> result) const override
  {
    VectorMem<20> v1(c1->Dimension());
    c1->Evaluate (ip, v1);
    result(0) = v1(comp);
  }  

  virtual void Evaluate (const BaseMappedIntegrationPoint & ip,
                         FlatVector<Complex> result) const override
  {
    Vector<Complex> v1(c1->Dimension());
    c1->Evaluate (ip, v1);
    result(0) = v1(comp);
  }

  virtual void Evaluate (const BaseMappedIntegrationRule & ir,
                         BareSliceMatrix<Complex> result) const override
  {
    // int dim1 = c1->Dimension();
    STACK_ARRAY(double, hmem, 2*ir.Size()*dim1);
    FlatMatrix<Complex> temp(ir.Size(), dim1, (Complex*)hmem);
    c1->Evaluate (ir, temp);
    result.Col(0).AddSize(ir.Size()) = temp.Col(comp);
  }  

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    STACK_ARRAY(T, hmem, ir.Size()*dim1);
    FlatMatrix<T,ORD> temp(dim1, ir.Size(), &hmem[0]);
    
    c1->Evaluate (ir, temp);
    size_t nv = ir.Size();
    __assume(nv > 0);
    for (size_t i = 0; i < nv; i++)
      values(0,i) = temp(comp, i);
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    auto in0 = input[0];    
    values.Row(0).AddSize(ir.Size()) = in0.Row(comp);
  }

  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    return MakeComponentCoefficientFunction (c1->Diff(var, dir), comp);
  }  
  
  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    Vector<bool> v1(c1->Dimension()), d1(c1->Dimension()), dd1(c1->Dimension());
    c1->NonZeroPattern (ud, v1, d1, dd1);
    nonzero(0) = v1(comp);
    nonzero_deriv(0) = d1(comp);
    nonzero_dderiv(0) = dd1(comp);
  }  

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    values(0) = input[0](comp);
  }
};

  shared_ptr<CoefficientFunction>
  MakeComponentCoefficientFunction (shared_ptr<CoefficientFunction> c1, int comp)
  {
    return make_shared<ComponentCoefficientFunction> (c1, comp);
  }




// ************************ DomainWiseCoefficientFunction *************************************

class DomainWiseCoefficientFunction : public T_CoefficientFunction<DomainWiseCoefficientFunction>
{
  Array<shared_ptr<CoefficientFunction>> ci;
  typedef T_CoefficientFunction<DomainWiseCoefficientFunction> BASE;
  using BASE::Evaluate;
public:
  DomainWiseCoefficientFunction() = default;
  DomainWiseCoefficientFunction (Array<shared_ptr<CoefficientFunction>> aci)
    : BASE(1, false), ci(aci) 
  { 
    for (auto & cf : ci)
      if (cf && cf->IsComplex()) is_complex = true;
    for (auto & cf : ci)
      if (cf) SetDimensions(cf->Dimensions());

    elementwise_constant = true;
    for (auto cf : ci)
      if (cf && !cf->ElementwiseConstant())
        elementwise_constant = false;
  }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    auto size = ci.Size();
    ar & size;
    ci.SetSize(size);
    for(auto& cf : ci)
      ar.Shallow(cf);
  }

  virtual bool DefinedOn (const ElementTransformation & trafo) override
  {
    int matindex = trafo.GetElementIndex();
    return (matindex < ci.Size() && ci[matindex]);
  }

  /*
  bool ElementwiseConstant() const override
  {
    for(auto cf : ci)
      if(cf && !cf->ElementwiseConstant())
        return false;
    return true;
  }
  */
  
  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    code.body += "// DomainWiseCoefficientFunction:\n";
    string type = "decltype(0.0";
    for(int in : inputs)
        type += "+decltype("+Var(in).S()+")()";
    type += ")";
    TraverseDimensions( Dimensions(), [&](int ind, int i, int j) {
        code.body += Var(index,i,j).Declare(type);
    });
    code.body += "switch(domain_index) {\n";
    for(int domain : Range(inputs))
    {
        code.body += "case " + ToLiteral(domain) + ": \n";
        TraverseDimensions( Dimensions(), [&](int ind, int i, int j) {
            code.body += "  "+Var(index, i, j).Assign(Var(inputs[domain], i, j), false);
        });
        code.body += "  break;\n";
    }
    code.body += "default: \n";
    TraverseDimensions( Dimensions(), [&](int ind, int i, int j) {
        code.body += "  "+Var(index, i, j).Assign(string("0.0"), false);
    });
    code.body += "  break;\n";
    code.body += "}\n";
  }

  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    for (auto & cf : ci)
      if (cf)
        cf->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  {
    Array<shared_ptr<CoefficientFunction>> cfa;
    for (auto cf : ci)
      cfa.Append (cf);
    return Array<shared_ptr<CoefficientFunction>>(cfa);
  } 
  
  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    Array<shared_ptr<CoefficientFunction>> ci_deriv;
    for (auto & cf : ci)
      if (cf)
        ci_deriv.Append (cf->Diff(var, dir));
      else
        ci_deriv.Append (nullptr);
    return make_shared<DomainWiseCoefficientFunction> (move (ci_deriv));
  }  
  
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    Vec<1> res;
    Evaluate (ip, res);
    return res(0);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<> result) const override
  {
    result = 0;
    int matindex = ip.GetTransformation().GetElementIndex();
    if (matindex < ci.Size() && ci[matindex])
      ci[matindex] -> Evaluate (ip, result);
  }

  virtual void Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<Complex> values) const override
  {
    int matindex = ir.GetTransformation().GetElementIndex();
    if (matindex < ci.Size() && ci[matindex])
      ci[matindex] -> Evaluate (ir, values);
    else
      values.AddSize(ir.Size(), Dimension()) = 0.0;
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    int matindex = ir.GetTransformation().GetElementIndex();
    if (matindex < ci.Size() && ci[matindex])
      ci[matindex] -> Evaluate (ir, values);
    else
      values.AddSize(Dimension(), ir.Size()) = T(0.0);
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    int matindex = ir.GetTransformation().GetElementIndex();
    if (matindex < ci.Size() && ci[matindex])
      values.AddSize(Dimension(), ir.Size()) = input[matindex];
    else
      values.AddSize(Dimension(), ir.Size()) = T(0.0);
  }  

  
  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    result = 0;
    int matindex = ip.GetTransformation().GetElementIndex();
    if (matindex < ci.Size() && ci[matindex])
      ci[matindex] -> Evaluate (ip, result);
  }
  
  virtual Complex EvaluateComplex (const BaseMappedIntegrationPoint & ip) const override
  {
    Vec<1,Complex> res;
    Evaluate (ip, res);
    return res(0);
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv,
                               FlatVector<bool> nonzero_dderiv) const override
  {
    size_t dim = Dimension();
    STACK_ARRAY(bool, mem, 3*dim);
    FlatVector<bool> nzi(dim, &mem[0]);
    FlatVector<bool> nzdi(dim, &mem[dim]);
    FlatVector<bool> nzddi(dim, &mem[2*dim]);
    nonzero = false;
    nonzero_deriv = false;
    nonzero_dderiv = false;
    for (auto & aci : ci)
      if (aci)
        {
          aci -> NonZeroPattern(ud, nzi, nzdi, nzddi);
          for (size_t i = 0; i < nonzero.Size(); i++)
            {
              nonzero(i) |= nzi(i);
              nonzero_deriv(i) |= nzdi(i);
              nonzero_dderiv(i) |= nzddi(i);
            }
        }
  }
  
  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override 
  {
    values = AutoDiffDiff<1,bool> (false);
    for (auto ini : input)
      for (size_t i = 0; i < values.Size(); i++)
        values(i) += ini(i);
  }
};

  shared_ptr<CoefficientFunction>
  MakeDomainWiseCoefficientFunction (Array<shared_ptr<CoefficientFunction>> aci)
  {
    return make_shared<DomainWiseCoefficientFunction> (move (aci));
  }






// ************************ OtherCoefficientFunction *************************************

class OtherCoefficientFunction : public T_CoefficientFunction<OtherCoefficientFunction>
{
  shared_ptr<CoefficientFunction> c1;
  typedef T_CoefficientFunction<OtherCoefficientFunction> BASE;
  using BASE::Evaluate;
public:
  OtherCoefficientFunction() = default;
  OtherCoefficientFunction (shared_ptr<CoefficientFunction> ac1)
    : BASE(ac1->Dimension(), ac1->IsComplex()), c1(ac1)
  { ; }

  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    ar.Shallow(c1);
  }

  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
  {
    throw Exception ("OtherCF::GenerateCode not available");
  }

  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    c1->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  {
    Array<shared_ptr<CoefficientFunction>> cfa;
    cfa.Append (c1);
    return Array<shared_ptr<CoefficientFunction>>(cfa);
  } 
  
  
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    throw Exception ("OtherCF::Evaluated (mip) not available");    
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<> result) const override
  {
    throw Exception ("OtherCF::Evaluated (mip) not available");        
  }

  /*
  virtual void Evaluate (const BaseMappedIntegrationRule & ir, FlatMatrix<double> values) const
  {
    if (!ir.GetOtherMIR()) throw Exception ("other mir not set, pls report to developers");
    c1->Evaluate (*ir.GetOtherMIR(), values);
  }
  */
  
  virtual void Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<Complex> values) const override
  {
    if (!ir.GetOtherMIR()) throw Exception ("other mir not set, pls report to developers");    
    c1->Evaluate (*ir.GetOtherMIR(), values);    
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    if (!ir.GetOtherMIR()) throw Exception ("other mir not set, pls report to developers");    
    c1->Evaluate (*ir.GetOtherMIR(), values);    
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    if (!ir.GetOtherMIR()) throw Exception ("other mir not set, pls report to developers");    
    c1->Evaluate (*ir.GetOtherMIR(), values);    
  }

  /*
  virtual void Evaluate (const SIMD_BaseMappedIntegrationRule & ir, FlatArray<AFlatMatrix<double>*> input,
                         AFlatMatrix<double> values) const 
  {
    // compile not available
    if (!ir.GetOtherMIR()) throw Exception ("other mir not set, pls report to developers");    
    c1->Evaluate (*ir.GetOtherMIR(), values);        
  }
  */
  
  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    throw Exception ("OtherCF::Evaluated (mip) not available");        
  }
  
  virtual Complex EvaluateComplex (const BaseMappedIntegrationPoint & ip) const override
  {
    throw Exception ("OtherCF::Evaluated (mip) not available");            
  }

  /*
  virtual void EvaluateDeriv(const BaseMappedIntegrationRule & mir,
                             FlatMatrix<> result,
                             FlatMatrix<> deriv) const
  {
    if (!mir.GetOtherMIR()) throw Exception ("other mir not set, pls report to developers");    
    c1->EvaluateDeriv (*mir.GetOtherMIR(), result, deriv);            
  }

  virtual void EvaluateDDeriv(const BaseMappedIntegrationRule & mir,
                              FlatMatrix<> result,
                              FlatMatrix<> deriv,
                              FlatMatrix<> dderiv) const
  {
    if (!mir.GetOtherMIR()) throw Exception ("other mir not set, pls report to developers");    
    c1->EvaluateDDeriv (*mir.GetOtherMIR(), result, deriv, dderiv);                
  }
  */ 
};

shared_ptr<CoefficientFunction>
MakeOtherCoefficientFunction (shared_ptr<CoefficientFunction> me)
{
  me->TraverseTree
    ( [&] (CoefficientFunction & nodecf)
      {
        if (dynamic_cast<const ProxyFunction*> (&nodecf))
          throw Exception ("Other() can be applied either to a proxy, or to an expression without any proxy\n  ---> use the Other()-operator on sub-trees");
      }
      );
  return make_shared<OtherCoefficientFunction> (me);
}







  

  // ///////////////////////////// IfPos   ////////////////////////////////  

  
class IfPosCoefficientFunction : public T_CoefficientFunction<IfPosCoefficientFunction>
  {
    shared_ptr<CoefficientFunction> cf_if;
    shared_ptr<CoefficientFunction> cf_then;
    shared_ptr<CoefficientFunction> cf_else;
    typedef T_CoefficientFunction<IfPosCoefficientFunction> BASE;
  public:
    IfPosCoefficientFunction() = default;
    IfPosCoefficientFunction (shared_ptr<CoefficientFunction> acf_if,
                              shared_ptr<CoefficientFunction> acf_then,
                              shared_ptr<CoefficientFunction> acf_else)
      : BASE(acf_then->Dimension(),
             acf_then->IsComplex() || acf_else->IsComplex()),
        cf_if(acf_if), cf_then(acf_then), cf_else(acf_else)
    {
      if (acf_then->Dimension() != acf_else->Dimension())
        throw Exception(string("In IfPosCoefficientFunction: dim(cf_then) == ") + ToLiteral(acf_then->Dimension()) + string(" != dim(cf_else) == ") + ToLiteral(acf_else->Dimension()));
      SetDimensions(cf_then->Dimensions());
    }

    void DoArchive(Archive& ar) override
    {
      BASE::DoArchive(ar);
      ar.Shallow(cf_if).Shallow(cf_then).Shallow(cf_else);
    }

    virtual ~IfPosCoefficientFunction () { ; }
    ///
    virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
    {
      if (cf_if->Evaluate(ip) > 0)
        return cf_then->Evaluate(ip);
      else
        return cf_else->Evaluate(ip);      
    }

    virtual void Evaluate (const BaseMappedIntegrationPoint& ip, FlatVector<double> values) const override
    {
      if(cf_if->Evaluate(ip) > 0)
        cf_then->Evaluate(ip,values);
      else
        cf_else->Evaluate(ip,values);
    }

    template <typename MIR, typename T, ORDERING ORD>    
    void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
    {
      size_t np = ir.Size();
      size_t dim = Dimension();
      
      STACK_ARRAY(T, hmem1, np);
      FlatMatrix<T,ORD> if_values(1, np, hmem1);
      STACK_ARRAY(T, hmem2, np*dim);
      FlatMatrix<T,ORD> then_values(dim, np, hmem2);
      STACK_ARRAY(T, hmem3, np*dim);
      FlatMatrix<T,ORD> else_values(dim, np, hmem3);
      
      cf_if->Evaluate (ir, if_values);
      cf_then->Evaluate (ir, then_values);
      cf_else->Evaluate (ir, else_values);
      
      for (size_t i = 0; i < np; i++)
        for (size_t j = 0; j < dim; j++)
          values(j,i) = ngstd::IfPos(if_values(0,i), then_values(j,i), else_values(j,i));
    }
    
    template <typename MIR, typename T, ORDERING ORD>
    void T_Evaluate (const MIR & ir,
                     FlatArray<BareSliceMatrix<T,ORD>> input,                       
                     BareSliceMatrix<T,ORD> values) const
    {
      size_t np = ir.Size();
      size_t dim = Dimension();

      auto if_values = input[0];
      auto then_values = input[1];
      auto else_values = input[2];
      
      for (size_t i = 0; i < np; i++)
        for (size_t j = 0; j < dim; j++)
          values(j,i) = ngstd::IfPos(if_values(0,i), then_values(j,i), else_values(j,i));
    }
    
    /*
    virtual void Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<double> hvalues) const override
    {
      auto values = hvalues.AddSize(ir.Size(), Dimension());
      
      STACK_ARRAY(double, hmem1, ir.Size());
      FlatMatrix<> if_values(ir.Size(), 1, hmem1);
      STACK_ARRAY(double, hmem2, ir.Size()*values.Width());
      FlatMatrix<> then_values(ir.Size(), values.Width(), hmem2);
      STACK_ARRAY(double, hmem3, ir.Size()*values.Width());
      FlatMatrix<> else_values(ir.Size(), values.Width(), hmem3);
      
      cf_if->Evaluate (ir, if_values);
      cf_then->Evaluate (ir, then_values);
      cf_else->Evaluate (ir, else_values);
      
      for (int i = 0; i < ir.Size(); i++)
        if (if_values(i) > 0)
          values.Row(i) = then_values.Row(i);
        else
          values.Row(i) = else_values.Row(i);

      // for (int i = 0; i < ir.Size(); i++)
      //   values(i) = (if_values(i) > 0) ? then_values(i) : else_values(i);
    }
    */
    
      using T_CoefficientFunction<IfPosCoefficientFunction>::Evaluate;
    virtual void Evaluate (const BaseMappedIntegrationPoint & ip, FlatVector<Complex> values) const override
    {
      if(cf_if->Evaluate(ip)>0)
        cf_then->Evaluate(ip,values);
      else
        cf_else->Evaluate(ip,values);
    }

    /*
    virtual void Evaluate (const SIMD_BaseMappedIntegrationRule & ir, BareSliceMatrix<SIMD<double>> values) const override
    {
      size_t nv = ir.Size(), dim = Dimension();
      STACK_ARRAY(SIMD<double>, hmem1, nv);
      ABareMatrix<double> if_values(&hmem1[0], nv);
      STACK_ARRAY(SIMD<double>, hmem2, nv*dim);
      ABareMatrix<double> then_values(&hmem2[0], nv);
      STACK_ARRAY(SIMD<double>, hmem3, nv*dim);
      ABareMatrix<double> else_values(&hmem3[0], nv);
      
      cf_if->Evaluate (ir, if_values);
      cf_then->Evaluate (ir, then_values);
      cf_else->Evaluate (ir, else_values);
      for (size_t k = 0; k < dim; k++)
        for (size_t i = 0; i < nv; i++)
          values(k,i) = ngstd::IfPos (if_values.Get(i),
                                      then_values.Get(k,i),
                                      else_values.Get(k,i));
    }

    virtual void Evaluate (const SIMD_BaseMappedIntegrationRule & ir, FlatArray<AFlatMatrix<double>*> input,
                           AFlatMatrix<double> values) const override
    {
      size_t nv = ir.Size(), dim = Dimension();      
      auto if_values = *input[0];
      auto then_values = *input[1];
      auto else_values = *input[2];
      
      for (size_t k = 0; k < dim; k++)
        for (size_t i = 0; i < nv; i++)
          values.Get(k,i) = ngstd::IfPos (if_values.Get(i),
                                          then_values.Get(k,i),
                                          else_values.Get(k,i)); 
    }
    
    virtual void Evaluate (const BaseMappedIntegrationRule & ir, FlatArray<FlatMatrix<>*> input,
                           FlatMatrix<double> values) const 
    {
      FlatMatrix<> if_values = *input[0];
      FlatMatrix<> then_values = *input[1];
      FlatMatrix<> else_values = *input[2];
      for (int i = 0; i < if_values.Height(); i++)
        values.Row(i) = (if_values(i) > 0) ? then_values.Row(i) : else_values.Row(i);
    }
    */

    // virtual bool IsComplex() const { return cf_then->IsComplex() | cf_else->IsComplex(); }
    // virtual int Dimension() const { return cf_then->Dimension(); }

    void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
    {
      auto cast_value = [&] (int i) {
          return code.res_type + "(" + Var(inputs[i]).S() + ")";
      };

      auto var_if = Var(inputs[0]);
      TraverseDimensions( cf_then->Dimensions(), [&](int ind, int i, int j) {
          code.body += Var(index,i,j).Declare(code.res_type);
      });

      if(code.is_simd) {
        TraverseDimensions( cf_then->Dimensions(), [&](int ind, int i, int j) {
            // cast all input parameters of IfPos to enforce the right overload (f.i. intermediate results could be double instead of AutoDiff<>)
            code.body += Var(index,i,j).Assign("IfPos("+cast_value(0) + ',' + cast_value(1) + ',' + cast_value(2)+')', false);
        });
      } else {
        code.body += "if (" + var_if.S() + ">0.0) {\n";
        TraverseDimensions( cf_then->Dimensions(), [&](int ind, int i, int j) {
            code.body += Var(index,i,j).Assign( Var(inputs[1],i,j), false );
        });
        code.body += "} else {\n";
        TraverseDimensions( cf_then->Dimensions(), [&](int ind, int i, int j) {
            code.body += Var(index,i,j).Assign( Var(inputs[2],i,j), false );
        });
        code.body += "}\n";
      }
    }

    /*
    virtual Array<int> Dimensions() const
    {
      return cf_then->Dimensions();
    }
    */

    /*
    [[deprecated]]
    virtual void EvaluateDeriv (const BaseMappedIntegrationRule & ir,
                                FlatMatrix<> values,
                                FlatMatrix<> deriv) const override
    {
      STACK_ARRAY(double, hmem1, ir.Size());
      FlatMatrix<> if_values(ir.Size(), 1, hmem1);
      STACK_ARRAY(double, hmem2, ir.Size()*values.Width());
      FlatMatrix<> then_values(ir.Size(), values.Width(), hmem2);
      STACK_ARRAY(double, hmem3, ir.Size()*values.Width());
      FlatMatrix<> else_values(ir.Size(), values.Width(), hmem3);
      STACK_ARRAY(double, hmem4, ir.Size()*values.Width());
      FlatMatrix<> then_deriv(ir.Size(), values.Width(), hmem4);
      STACK_ARRAY(double, hmem5, ir.Size()*values.Width());
      FlatMatrix<> else_deriv(ir.Size(), values.Width(), hmem5);

      
      cf_if->Evaluate (ir, if_values);
      cf_then->EvaluateDeriv (ir, then_values, then_deriv);
      cf_else->EvaluateDeriv (ir, else_values, else_deriv);
      
      for (int i = 0; i < ir.Size(); i++)
        if (if_values(i) > 0)
          {
            values.Row(i) = then_values.Row(i);
            deriv.Row(i) = then_deriv.Row(i);
          }
        else
          {
            values.Row(i) = else_values.Row(i);
            deriv.Row(i) = else_deriv.Row(i);
          }
    }
    */
    
    /*
    virtual void EvaluateDeriv (const BaseMappedIntegrationRule & ir,
                                FlatMatrix<Complex> result,
                                FlatMatrix<Complex> deriv) const
    {
      Evaluate (ir, result);
      deriv = 0;
    }

    virtual void EvaluateDDeriv (const BaseMappedIntegrationRule & ir,
                                 FlatMatrix<> result,
                                 FlatMatrix<> deriv,
                                 FlatMatrix<> dderiv) const
    {
      EvaluateDeriv (ir, result, deriv);
      dderiv = 0;
    }

    virtual void EvaluateDDeriv (const BaseMappedIntegrationRule & ir,
                                 FlatMatrix<Complex> result,
                                 FlatMatrix<Complex> deriv,
                                 FlatMatrix<Complex> dderiv) const
    {
      EvaluateDeriv (ir, result, deriv);
      dderiv = 0;
    }

    
    virtual void EvaluateDeriv (const BaseMappedIntegrationRule & ir,
                                 FlatArray<FlatMatrix<>*> input,
                                 FlatArray<FlatMatrix<>*> dinput,
                                 FlatMatrix<> result,
                                 FlatMatrix<> deriv) const
    {
      EvaluateDeriv (ir, result, deriv);
    }

    virtual void EvaluateDDeriv (const BaseMappedIntegrationRule & ir,
                                 FlatArray<FlatMatrix<>*> input,
                                 FlatArray<FlatMatrix<>*> dinput,
                                 FlatArray<FlatMatrix<>*> ddinput,
                                 FlatMatrix<> result,
                                 FlatMatrix<> deriv,
                                 FlatMatrix<> dderiv) const
    {
      EvaluateDDeriv (ir, result, deriv, dderiv);
    }
    */

    // virtual bool ElementwiseConstant () const { return false; }
    
    // virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero) const;

    /*
    virtual void PrintReport (ostream & ost) const;
    virtual void PrintReportRec (ostream & ost, int level) const;
    virtual string GetName () const;
    */

    /*
    virtual void EvaluateDeriv (const SIMD_BaseMappedIntegrationRule & ir,
                                AFlatMatrix<> values,
                                AFlatMatrix<> deriv) const
    {
      int dim = Dimension();
      STACK_ARRAY(SIMD<double>, hmem1, ir.Size());
      AFlatMatrix<> if_values(1, ir.IR().GetNIP(), hmem1);
      STACK_ARRAY(SIMD<double>, hmem2, ir.Size()*dim);
      AFlatMatrix<> then_values(dim, ir.IR().GetNIP(), hmem2);
      STACK_ARRAY(SIMD<double>, hmem3, ir.Size()*dim);
      AFlatMatrix<> else_values(dim, ir.IR().GetNIP(), hmem3);
      STACK_ARRAY(SIMD<double>, hmem4, ir.Size()*dim);
      AFlatMatrix<> then_deriv(dim, ir.IR().GetNIP(), hmem4);
      STACK_ARRAY(SIMD<double>, hmem5, ir.Size()*dim);
      AFlatMatrix<> else_deriv(dim, ir.IR().GetNIP(), hmem5);

      cf_if->Evaluate (ir, if_values);
      cf_then->EvaluateDeriv (ir, then_values, then_deriv);
      cf_else->EvaluateDeriv (ir, else_values, else_deriv);
      
      for (int i = 0; i < ir.Size(); i++)
        for (int j = 0; j < dim; j++)
          {
            values.Get(j,i) = IfPos(if_values.Get(0,i), then_values.Get(j,i), else_values.Get(j,i));
            deriv.Get(j,i) = IfPos(if_values.Get(0,i), then_deriv.Get(j,i), else_deriv.Get(j,i));
          }
    }

    virtual void EvaluateDeriv (const SIMD_BaseMappedIntegrationRule & ir,
                                FlatArray<AFlatMatrix<>*> input,
                                FlatArray<AFlatMatrix<>*> dinput,
                                AFlatMatrix<> result,
                                AFlatMatrix<> deriv) const
    {
      size_t nv = ir.Size(), dim = Dimension();      
      auto if_values = *input[0];
      auto then_values = *input[1];
      auto else_values = *input[2];
      auto then_deriv = *dinput[1];
      auto else_deriv = *dinput[2];
      
      for (size_t k = 0; k < dim; k++)
        for (size_t i = 0; i < nv; i++)
          {
            result.Get(k,i) = ngstd::IfPos (if_values.Get(i),
                                            then_values.Get(k,i),
                                            else_values.Get(k,i));
            deriv.Get(k,i) = ngstd::IfPos (if_values.Get(i),
                                           then_deriv.Get(k,i),
                                           else_deriv.Get(k,i));
          }
    }
    */
    
    virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
    {
      cf_if->TraverseTree (func);
      cf_then->TraverseTree (func);
      cf_else->TraverseTree (func);
      func(*this);
    }
    
    virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
    {
      return Array<shared_ptr<CoefficientFunction>>( { cf_if, cf_then, cf_else } );
    }
    
    virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                                 FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
    {
      int dim = Dimension();
      Vector<bool> v1(dim), d1(dim), dd1(dim);
      Vector<bool> v2(dim), d2(dim), dd2(dim);
      cf_then->NonZeroPattern (ud, v1, d1, dd1);
      cf_else->NonZeroPattern (ud, v2, d2, dd2);
      for (int i = 0; i < dim; i++)
        {
          nonzero(i) = v1(i) || v2(i);
          nonzero_deriv(i) = d1(i) || d2(i);
          nonzero_dderiv(i) = dd1(i) || dd2(i);
        }
    }  

    virtual void NonZeroPattern (const class ProxyUserData & ud,
                                 FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                                 FlatVector<AutoDiffDiff<1,bool>> values) const override
    {
      auto v1 = input[1];
      auto v2 = input[2];
      values = v1+v2;
    }
  };
  
  extern
  shared_ptr<CoefficientFunction> IfPos (shared_ptr<CoefficientFunction> cf_if,
                                         shared_ptr<CoefficientFunction> cf_then,
                                         shared_ptr<CoefficientFunction> cf_else)
  {
    return make_shared<IfPosCoefficientFunction> (cf_if, cf_then, cf_else);
  }


class VectorialCoefficientFunction : public T_CoefficientFunction<VectorialCoefficientFunction>
{
  Array<shared_ptr<CoefficientFunction>> ci;
  Array<size_t> dimi;  // dimensions of components
  typedef T_CoefficientFunction<VectorialCoefficientFunction> BASE;
public:
  VectorialCoefficientFunction() = default;
  VectorialCoefficientFunction (Array<shared_ptr<CoefficientFunction>> aci)
    : BASE(0, false), ci(aci), dimi(aci.Size())
  {
    int hdim = 0;
    for (int i : Range(ci))
      {
        dimi[i] = ci[i]->Dimension();
        hdim += dimi[i];
      }
    
    for (auto cf : ci)
      if (cf && cf->IsComplex())
        is_complex = true;

    SetDimension(hdim);

    elementwise_constant = true;
    for (auto cf : ci)
      if (!cf->ElementwiseConstant())
        elementwise_constant = false;
    // dims = Array<int> ( { dimension } ); 
  }
  
  void DoArchive(Archive& ar) override
  {
    BASE::DoArchive(ar);
    auto size = ci.Size();
    ar & size;
    ci.SetSize(size);
    for(auto& cf : ci)
      ar.Shallow(cf);
    ar & dimi;
  }

  virtual string GetDescription () const override
  { return "VectorialCoefficientFunction"; }
  
  virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override;

  virtual void TraverseTree (const function<void(CoefficientFunction&)> & func) override
  {
    for (auto cf : ci)
      cf->TraverseTree (func);
    func(*this);
  }

  virtual Array<shared_ptr<CoefficientFunction>> InputCoefficientFunctions() const override
  {
    Array<shared_ptr<CoefficientFunction>> cfa;
    for (auto cf : ci)
      cfa.Append (cf);
    return Array<shared_ptr<CoefficientFunction>>(cfa);
  } 


  virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                               FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
  {
    int base = 0;
    for (auto cf : ci)
      {
        int dimi = cf->Dimension();
        cf->NonZeroPattern(ud,
                           nonzero.Range(base,base+dimi),
                           nonzero_deriv.Range(base,base+dimi),
                           nonzero_dderiv.Range(base,base+dimi));
        base += dimi;
      }
  }

  virtual void NonZeroPattern (const class ProxyUserData & ud,
                               FlatArray<FlatVector<AutoDiffDiff<1,bool>>> input,
                               FlatVector<AutoDiffDiff<1,bool>> values) const override
  {
    size_t base = 0;
    for (size_t i : Range(ci))
      {
        values.Range(base,base+dimi[i]) = input[i];
        base += dimi[i];
      }    
  }
  
  
  virtual bool DefinedOn (const ElementTransformation & trafo) override
  {
    for (auto & cf : ci)
      if (!cf->DefinedOn(trafo)) return false;
    return true;
  }
  

  using BASE::Evaluate;  
  virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
  {
    Vec<1> res;
    Evaluate (ip, res);
    return res(0);
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<> result) const override
  {
    int base = 0;
    for (auto & cf : ci)
      {
        int dimi = cf->Dimension();
        cf->Evaluate(ip, result.Range(base,base+dimi));
        base += dimi;
      }
  }

  virtual void Evaluate(const BaseMappedIntegrationPoint & ip,
                        FlatVector<Complex> result) const override
  {
    int base = 0;
    for (auto cf : ci)
      {
        int dimi = cf->Dimension();
        cf->Evaluate(ip, result.Range(base,base+dimi));
        base += dimi;
      }

    // for (int i : Range(ci))
    // ci[i]->Evaluate(ip, result.Range(i,i+1));
  }

  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
  {
    size_t base = 0;
    for (auto i : Range(ci.Size()))
      {
        ci[i]->Evaluate(ir, values.Rows(base, base + dimi[i]));
        base += dimi[i];
      }
  }
 
  template <typename MIR, typename T, ORDERING ORD>
  void T_Evaluate (const MIR & ir,
                   FlatArray<BareSliceMatrix<T,ORD>> input,                       
                   BareSliceMatrix<T,ORD> values) const
  {
    size_t base = 0;
    size_t np = ir.Size();
    for (size_t i : Range(ci))
      {
        values.Rows(base,base+dimi[i]).AddSize(dimi[i], np) = input[i];
        base += dimi[i];
      }
  }

  virtual void Evaluate(const BaseMappedIntegrationRule & ir,
                        BareSliceMatrix<Complex> result) const override
  {
    int base = 0;
    for (auto cf : ci)
      {
        int dimi = cf->Dimension();
        STACK_ARRAY(double, hmem, 2*ir.Size()*dimi);
        FlatMatrix<Complex> temp(ir.Size(), dimi, (Complex*)hmem);
        cf->Evaluate(ir, temp);
        result.Cols(base,base+dimi).AddSize(ir.Size(), dimi) = temp;
        base += dimi;
      }
  }

  shared_ptr<CoefficientFunction> Diff (const CoefficientFunction * var,
                                          shared_ptr<CoefficientFunction> dir) const override
  {
    if (this == var) return dir;
    Array<shared_ptr<CoefficientFunction>> diff_ci;
    for (auto & cf : ci)
      if (cf)
        diff_ci.Append (cf->Diff(var, dir));
      else
        diff_ci.Append (nullptr);
    auto veccf = make_shared<VectorialCoefficientFunction> (move(diff_ci));
    veccf->SetDimensions(Dimensions());
    return veccf;
  }  
};

  void VectorialCoefficientFunction::GenerateCode(Code &code, FlatArray<int> inputs, int index) const
  {
    int input = 0;
    int input_index = 0;
    FlatArray<int> dims = Dimensions();
    TraverseDimensions( dims, [&](int ind, int i, int j) {
	auto cfi = ci[input];
        int i1, j1;
        GetIndex( cfi->Dimensions(), input_index, i1, j1 );
        code.body += Var(index,i,j).Assign( Var(inputs[input], i1, j1) );
        input_index++;
        if (input_index == cfi->Dimension() )
        {
            input++;
            input_index = 0;
        }
    });

  }


  shared_ptr<CoefficientFunction>
  MakeVectorialCoefficientFunction (Array<shared_ptr<CoefficientFunction>> aci)
  {
    return make_shared<VectorialCoefficientFunction> (move (aci));
  }


// ////////////////////////// Coordinate CF ////////////////////////

  class CoordCoefficientFunction
    : public T_CoefficientFunction<CoordCoefficientFunction, CoefficientFunctionNoDerivative>
  {
    int dir;
    typedef T_CoefficientFunction<CoordCoefficientFunction, CoefficientFunctionNoDerivative> BASE;
  public:
    CoordCoefficientFunction() = default;
    CoordCoefficientFunction (int adir) : BASE(1, false), dir(adir) { ; }

    void DoArchive(Archive& ar) override
    {
      BASE::DoArchive(ar);
      ar & dir;
    }

    virtual string GetDescription () const override
    {
      string dirname;
      switch (dir)
        {
        case 0: dirname = "x"; break;
        case 1: dirname = "y"; break;
        case 2: dirname = "z"; break;
        default: dirname = ToLiteral(dir);
        }
      return string("coordinate ")+dirname;
    }

    using BASE::Evaluate;
    virtual double Evaluate (const BaseMappedIntegrationPoint & ip) const override
    {
      if (!ip.IsComplex())
        return ip.GetPoint()(dir);
      else
        return ip.GetPointComplex()(dir).real();
    }
    /*
    virtual void Evaluate(const BaseMappedIntegrationRule & ir,
                          FlatMatrix<> result) const
    {
      if (!ir.IsComplex())
        result.Col(0) = ir.GetPoints().Col(dir);
      else
      {
        auto pnts = ir.GetPointsComplex().Col(dir);
        for (auto i : Range(ir.Size()))
          result(i,0) = pnts(i).real();
      }
    }
    virtual void Evaluate(const BaseMappedIntegrationRule & ir,
			  FlatMatrix<Complex> result) const override
    {
      result.Col(0) = ir.GetPoints().Col(dir);
    }
    */

    virtual void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override {
        auto v = Var(index);
        // code.body += v.Assign(CodeExpr(string("mir.GetPoints()(i,")+ToLiteral(dir)+")"));
        code.body += v.Assign(CodeExpr(string("points(i,")+ToLiteral(dir)+")"));
    }

    template <typename MIR, typename T, ORDERING ORD>
    void T_Evaluate (const MIR & ir, BareSliceMatrix<T,ORD> values) const
    {
      if(!ir.IsComplex())
        {
          auto points = ir.GetPoints();
          size_t nv = ir.Size();
          __assume (nv > 0);
          for (size_t i = 0; i < nv; i++)
            values(0,i) = points(i, dir);
        }
      else
        {
          auto cpoints = ir.GetPointsComplex();
          size_t nv = ir.Size();
          __assume (nv > 0);
          for(auto i : Range(nv))
            values(0,i) = cpoints(i,dir).real();
        }
    }

    template <typename MIR, typename T, ORDERING ORD>
    void T_Evaluate (const MIR & ir,
                     FlatArray<BareSliceMatrix<T,ORD>> input,                       
                     BareSliceMatrix<T,ORD> values) const
    { T_Evaluate (ir, values); }

    /*
    virtual void Evaluate (const SIMD_BaseMappedIntegrationRule & ir, FlatArray<AFlatMatrix<double>*> input,
                           AFlatMatrix<double> values) const
    {
      Evaluate (ir, values);
    }
    */
  };


shared_ptr<CoefficientFunction> MakeCoordinateCoefficientFunction (int comp)
{
  return make_shared<CoordCoefficientFunction> (comp);
}


  // ///////////////////////////// Compiled CF /////////////////////////
  class CompiledCoefficientFunction : public CoefficientFunction, public std::enable_shared_from_this<CompiledCoefficientFunction>
  {
    typedef void (*lib_function)(const ngfem::BaseMappedIntegrationRule &, ngbla::BareSliceMatrix<double>);
    typedef void (*lib_function_simd)(const ngfem::SIMD_BaseMappedIntegrationRule &, BareSliceMatrix<SIMD<double>>);
    typedef void (*lib_function_deriv)(const ngfem::BaseMappedIntegrationRule &, ngbla::BareSliceMatrix<AutoDiff<1,double>>);
    typedef void (*lib_function_simd_deriv)(const ngfem::SIMD_BaseMappedIntegrationRule &, BareSliceMatrix<AutoDiff<1,SIMD<double>>>);
    typedef void (*lib_function_dderiv)(const ngfem::BaseMappedIntegrationRule &, ngbla::BareSliceMatrix<AutoDiffDiff<1,double>>);
    typedef void (*lib_function_simd_dderiv)(const ngfem::SIMD_BaseMappedIntegrationRule &, BareSliceMatrix<AutoDiffDiff<1,SIMD<double>>>);

    typedef void (*lib_function_complex)(const ngfem::BaseMappedIntegrationRule &, ngbla::BareSliceMatrix<Complex>);
    typedef void (*lib_function_simd_complex)(const ngfem::SIMD_BaseMappedIntegrationRule &, BareSliceMatrix<SIMD<Complex>>);

    shared_ptr<CoefficientFunction> cf;
    Array<CoefficientFunction*> steps;
    DynamicTable<int> inputs;
    size_t max_inputsize;
    Array<int> dim;
    int totdim;
    Array<bool> is_complex;
    // Array<Timer*> timers;
    unique_ptr<SharedLibrary> library;
    lib_function compiled_function = nullptr;
    lib_function_simd compiled_function_simd = nullptr;
    lib_function_deriv compiled_function_deriv = nullptr;
    lib_function_simd_deriv compiled_function_simd_deriv = nullptr;
    lib_function_dderiv compiled_function_dderiv = nullptr;
    lib_function_simd_dderiv compiled_function_simd_dderiv = nullptr;

    lib_function_complex compiled_function_complex = nullptr;
    lib_function_simd_complex compiled_function_simd_complex = nullptr;

  public:
    CompiledCoefficientFunction() = default;
    CompiledCoefficientFunction (shared_ptr<CoefficientFunction> acf)
      : CoefficientFunction(acf->Dimension(), acf->IsComplex()), cf(acf) // , compiled_function(nullptr), compiled_function_simd(nullptr)
    {
      SetDimensions (cf->Dimensions());
      cf -> TraverseTree
        ([&] (CoefficientFunction & stepcf)
         {
           if (!steps.Contains(&stepcf))
             {
               steps.Append (&stepcf);
               // timers.Append (new Timer(string("CompiledCF")+typeid(stepcf).name()));
               dim.Append (stepcf.Dimension());
               is_complex.Append (stepcf.IsComplex());
             }
         });
      
      totdim = 0;
      for (int d : dim) totdim += d;
      
      cout << IM(3) << "Compiled CF:" << endl;
      for (auto cf : steps)
        cout << IM(3) << typeid(*cf).name() << endl;
      
      inputs = DynamicTable<int> (steps.Size());
      max_inputsize = 0;
      
      cf -> TraverseTree
        ([&] (CoefficientFunction & stepcf)
         {
           int mypos = steps.Pos (&stepcf);
           if (!inputs[mypos].Size())
             {
               Array<shared_ptr<CoefficientFunction>> in = stepcf.InputCoefficientFunctions();
               max_inputsize = max2(in.Size(), max_inputsize);
               for (auto incf : in)
                 inputs.Add (mypos, steps.Pos(incf.get()));
             }
         });
      cout << IM(3) << "inputs = " << endl << inputs << endl;

    }


  void PrintReport (ostream & ost) const override
  {
    ost << "Compiled CF:" << endl;
    for (int i : Range(steps))
      {
        auto & cf = steps[i];
        ost << "Step " << i << ": " << cf->GetDescription();
        if (cf->Dimensions().Size() == 1)
          ost << ", dim=" << cf->Dimension();
        else if (cf->Dimensions().Size() == 2)
          ost << ", dims = " << cf->Dimensions()[0] << " x " << cf->Dimensions()[1];
        ost << endl;
        if (inputs[i].Size() > 0)
          {
            ost << "     input: ";
            for (auto innr : inputs[i])
              ost << innr << " ";
            ost << endl;
          }
      }
    /*
    for (auto cf : steps)
      ost << cf -> GetDescription() << endl;
    ost << "inputs = " << endl << inputs << endl;
    */
    
      /*
    for (int i = 0; i < 2*level; i++)
      ost << ' ';
    ost << "coef " << GetDescription() << ","
        << (IsComplex() ? " complex" : " real");
    if (Dimensions().Size() == 1)
      ost << ", dim=" << Dimension();
    else if (Dimensions().Size() == 2)
      ost << ", dims = " << Dimensions()[0] << " x " << Dimensions()[1];
    ost << endl;

    Array<shared_ptr<CoefficientFunction>> input = InputCoefficientFunctions();
    for (int i = 0; i < input.Size(); i++)
      input[i] -> PrintReportRec (ost, level+1);
    */
  }

    
    void DoArchive(Archive& ar) override
    {
      CoefficientFunction::DoArchive(ar);
      ar.Shallow(cf);
      if(ar.Input())
        {
          cf -> TraverseTree
            ([&] (CoefficientFunction & stepcf)
             {
               if (!steps.Contains(&stepcf))
                 {
                   steps.Append (&stepcf);
                   // timers.Append (new Timer(string("CompiledCF")+typeid(stepcf).name()));
                   dim.Append (stepcf.Dimension());
                   is_complex.Append (stepcf.IsComplex());
                 }
             });
          totdim = 0;
          for (int d : dim) totdim += d;
          inputs = DynamicTable<int> (steps.Size());
          max_inputsize = 0;

          cf -> TraverseTree
            ([&] (CoefficientFunction & stepcf)
             {
               int mypos = steps.Pos (&stepcf);
               if (!inputs[mypos].Size())
                 {
                   Array<shared_ptr<CoefficientFunction>> in = stepcf.InputCoefficientFunctions();
                   max_inputsize = max2(in.Size(), max_inputsize);
                   for (auto incf : in)
                     inputs.Add (mypos, steps.Pos(incf.get()));
                 }
             });
        }
    }

    void RealCompile(int maxderiv, bool wait)
    {
        std::vector<string> link_flags;
        if(cf->IsComplex())
            maxderiv = 0;
        stringstream s;
        string pointer_code;
        string top_code = ""
             "#include<fem.hpp>\n"
             "using namespace ngfem;\n"
             "extern \"C\" {\n"
             ;

        string parameters[3] = {"results", "deriv", "dderiv"};

        for (int deriv : Range(maxderiv+1))
        for (auto simd : {false,true}) {
            cout << IM(3) << "Compiled CF:" << endl;
            Code code;
            code.is_simd = simd;
            code.deriv = deriv;

            string res_type = cf->IsComplex() ? "Complex" : "double";
            if(simd) res_type = "SIMD<" + res_type + ">";
            if(deriv==1) res_type = "AutoDiff<1," + res_type + ">";
            if(deriv==2) res_type = "AutoDiffDiff<1," + res_type + ">";
            code.res_type = res_type;

            for (auto i : Range(steps)) {
              auto& step = *steps[i];
              cout << IM(3) << "step " << i << ": " << typeid(step).name() << endl;
              step.GenerateCode(code, inputs[i],i);
            }

            pointer_code += code.pointer;
            top_code += code.top;

            // set results
            string scal_type = cf->IsComplex() ? "Complex" : "double";
            int ii = 0;
            TraverseDimensions( cf->Dimensions(), [&](int ind, int i, int j) {
                 code.body += Var(steps.Size(),i,j).Declare(res_type);
                 code.body += Var(steps.Size(),i,j).Assign(Var(steps.Size()-1,i,j),false);
                 string sget = "(i," + ToLiteral(ii) + ") =";
                 if(simd) sget = "(" + ToLiteral(ii) + ",i) =";

                 // for (auto ideriv : Range(simd ? 1 : deriv+1))
                 for (auto ideriv : Range(1))
                 {
                   code.body += parameters[ideriv] + sget + Var(steps.Size(),i,j).code;
                   /*
                   if(deriv>=1 && !simd)
                   {
                     code.body += ".";
                     if(ideriv==2) code.body += "D";
                     if(ideriv>=1) code.body += "DValue(0)";
                     else code.body += "Value()";
                   }
                   */
                   // if(simd) code.body +=".Data()";
                   code.body += ";\n";
                 }
                 ii++;
            });

            if(code.header.find("gridfunction_local_heap") != std::string::npos)
            {
                code.header.insert(0, "LocalHeapMem<100000> gridfunction_local_heap(\"compiled_cf_gfheap\");\n");
                code.header.insert(0, "ArrayMem<int, 100> gridfunction_dnums;\n");
                code.header.insert(0, "ArrayMem<double, 100> gridfunction_elu;\n");
            }

            // Function name
#ifdef WIN32
            s << "__declspec(dllexport) ";
#endif
            s << "void CompiledEvaluate";
            if(deriv==2) s << "D";
            if(deriv>=1) s << "Deriv";
            if(simd) s << "SIMD";

            // Function parameters
            if (simd)
              {
                s << "(SIMD_BaseMappedIntegrationRule & mir, BareSliceMatrix<" << res_type << "> results";
              }
            else
              {
                s << "(BaseMappedIntegrationRule & mir, BareSliceMatrix<" << res_type << "> results";
                /*
                string param_type = simd ? "BareSliceMatrix<SIMD<"+scal_type+">> " : "FlatMatrix<"+scal_type+"> ";
                if (simd && deriv == 0) param_type = "BareSliceMatrix<SIMD<"+scal_type+">> ";
                s << "( " << (simd?"SIMD_":"") << "BaseMappedIntegrationRule &mir";
                for(auto i : Range(deriv+1))
                  s << ", " << param_type << parameters[i];
                */
              }
            s << " ) {" << endl;
            s << code.header << endl;
            s << "auto points = mir.GetPoints();" << endl;
            s << "auto domain_index = mir.GetTransformation().GetElementIndex();" << endl;
            s << "for ( auto i : Range(mir)) {" << endl;
            s << "auto & ip = mir[i];" << endl;
            s << code.body << endl;
            s << "}\n}" << endl << endl;

            for(const auto &lib : code.link_flags)
                if(std::find(std::begin(link_flags), std::end(link_flags), lib) == std::end(link_flags))
                    link_flags.push_back(lib);

        }
        s << "}" << endl;
        string file_code = top_code + s.str();
        std::vector<string> codes;
        codes.push_back(file_code);
        if(pointer_code.size()) {
          pointer_code = "extern \"C\" {\n" + pointer_code;
          pointer_code += "}\n";
          codes.push_back(pointer_code);
        }

        auto self = shared_from_this();
        auto compile_func = [self, codes, link_flags, maxderiv] () {
              self->library = CompileCode( codes, link_flags );
              if(self->cf->IsComplex())
              {
                  self->compiled_function_simd_complex = self->library->GetFunction<lib_function_simd_complex>("CompiledEvaluateSIMD");
                  self->compiled_function_complex = self->library->GetFunction<lib_function_complex>("CompiledEvaluate");
              }
              else
              {
                  self->compiled_function_simd = self->library->GetFunction<lib_function_simd>("CompiledEvaluateSIMD");
                  self->compiled_function = self->library->GetFunction<lib_function>("CompiledEvaluate");
                  if(maxderiv>0)
                  {
                      self->compiled_function_simd_deriv = self->library->GetFunction<lib_function_simd_deriv>("CompiledEvaluateDerivSIMD");
                      self->compiled_function_deriv = self->library->GetFunction<lib_function_deriv>("CompiledEvaluateDeriv");
                  }
                  if(maxderiv>1)
                  {
                      self->compiled_function_simd_dderiv = self->library->GetFunction<lib_function_simd_dderiv>("CompiledEvaluateDDerivSIMD");
                      self->compiled_function_dderiv = self->library->GetFunction<lib_function_dderiv>("CompiledEvaluateDDeriv");
                  }
              }
              cout << IM(7) << "Compilation done" << endl;
        };
        if(wait)
            compile_func();
        else
        {
          try {
            std::thread( compile_func ).detach();
          } catch (const std::exception &e) {
              cerr << IM(3) << "Compilation of CoefficientFunction failed: " << e.what() << endl;
          }
        }
    }

    void TraverseTree (const function<void(CoefficientFunction&)> & func) override
    {
      cf -> TraverseTree (func);
      func(*cf);
    }

    // virtual bool IsComplex() const { return cf->IsComplex(); }
    // virtual int Dimension() const { return cf->Dimension(); }
    // virtual Array<int> Dimensions() const  { return cf->Dimensions(); } 
    
    
    // bool ElementwiseConstant () const override { return false; }
    /*
    virtual void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                                 FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const
    { cf->NonZeroPattern (ud, nonzero, nonzero_deriv, nonzero_dderiv); }
    */
    
    double Evaluate (const BaseMappedIntegrationPoint & ip) const override
    {
      return cf -> Evaluate(ip);
    }

    void Evaluate(const BaseMappedIntegrationPoint & ip,
                  FlatVector<> result) const override
    {
      cf->Evaluate (ip, result);      
    }

    void Evaluate(const BaseMappedIntegrationPoint & ip,
                  FlatVector<Complex> result) const override
    {
      cf->Evaluate (ip, result);
    }

    template <typename MIR, typename T, ORDERING ORD>
    void T_Evaluate (const MIR & ir,
                     BareSliceMatrix<T,ORD> values) const
    {
      ArrayMem<T, 1000> hmem(ir.Size()*totdim);
      size_t mem_ptr = 0;
      ArrayMem<BareSliceMatrix<T,ORD>,100> temp(steps.Size());
      ArrayMem<BareSliceMatrix<T,ORD>, 100> in(max_inputsize);
      for (size_t i = 0; i < steps.Size()-1; i++)
        {
          new (&temp[i]) BareSliceMatrix<T,ORD> (FlatMatrix<T,ORD> (dim[i], ir.Size(), &hmem[mem_ptr]));
          mem_ptr += ir.Size()*dim[i];
        }
      
      new (&temp.Last()) BareSliceMatrix<T,ORD>(values);

      for (size_t i = 0; i < steps.Size(); i++)
        {
          auto inputi = inputs[i];
          for (int nr : Range(inputi))
            new (&in[nr]) BareSliceMatrix<T,ORD> (temp[inputi[nr]]);
          steps[i] -> Evaluate (ir, in.Range(0, inputi.Size()), temp[i]);
        }
    }
    
    void NonZeroPattern (const class ProxyUserData & ud, FlatVector<bool> nonzero,
                         FlatVector<bool> nonzero_deriv, FlatVector<bool> nonzero_dderiv) const override
    {
      typedef AutoDiffDiff<1,bool> T;
      ArrayMem<T, 1000> hmem(totdim);
      size_t mem_ptr = 0;
      ArrayMem<FlatVector<T>,100> temp(steps.Size());
      ArrayMem<FlatVector<T>,100> in(max_inputsize);
      for (size_t i = 0; i < steps.Size(); i++)
        {
          new (&temp[i]) FlatVector<T> (dim[i], &hmem[mem_ptr]);
          mem_ptr += dim[i];
        }
      
      for (size_t i = 0; i < steps.Size(); i++)
        {
          auto inputi = inputs[i];
          for (int nr : Range(inputi))
            new (&in[nr]) FlatVector<T> (temp[inputi[nr]]);
          steps[i] -> NonZeroPattern (ud, in.Range(0, inputi.Size()), temp[i]);
        }
      auto & last = temp.Last();
      for (size_t i = 0; i < nonzero.Size(); i++)
        {
          nonzero(i) = last(i).Value();
          nonzero_deriv(i) = last(i).DValue(0);
          nonzero_dderiv(i) = last(i).DDValue(0);
        }

      /*
      Vector<bool> nonzero2 = nonzero;
      Vector<bool> nonzero2_deriv = nonzero_deriv;
      Vector<bool> nonzero2_dderiv = nonzero_dderiv;
      cf->NonZeroPattern (ud, nonzero, nonzero_deriv, nonzero_dderiv);
      for (int i = 0; i < nonzero.Size(); i++)
        {
          if (nonzero(i) != nonzero2(i)) cout << "diff nz" << endl;
          if (nonzero_deriv(i) != nonzero2_deriv(i)) cout << "diff nzd" << endl;
          if (nonzero_dderiv(i) != nonzero2_dderiv(i)) cout << "diff nzdd" << endl;
        }
      */
    }

    
    void Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<double> values) const override
    {
      if(compiled_function)
      {
        compiled_function(ir,values);
        return;
      }

      T_Evaluate (ir, Trans(values));
      return;

      /*
      // static Timer t1("CompiledCF::Evaluate 1");
      // static Timer t2("CompiledCF::Evaluate 2");
      // static Timer t3("CompiledCF::Evaluate 3");

      // t1.Start();
      // int totdim = 0;
      // for (int d : dim) totdim += d;
      ArrayMem<double, 10000> hmem(ir.Size()*totdim);
      int mem_ptr = 0;
      ArrayMem<BareSliceMatrix<double,ColMajor>,100> temp(steps.Size());
      ArrayMem<BareSliceMatrix<double,ColMajor>, 100> in(max_inputsize);
      for (int i = 0; i < steps.Size(); i++)
        {
          // temp[i].AssignMemory(ir.Size(), dim[i], &hmem[mem_ptr]);
          new (&temp[i]) BareSliceMatrix<double,ColMajor> (dim[i], &hmem[mem_ptr], DummySize(dim[i], ir.Size()));          
          mem_ptr += ir.Size()*dim[i];
        }
      // t1.Stop();
      // t2.Start();
      for (int i = 0; i < steps.Size(); i++)
        {
          // myglobalvar ++;
          // timers[i]->Start();
          auto inputi = inputs[i];
          for (int nr : Range(inputi))
            // in[nr] = &temp[inputi[nr]];
            new (&in[nr]) BareSliceMatrix<double,ColMajor> (temp[inputi[nr]]);
          steps[i] -> Evaluate (ir, in.Range(0, inputi.Size()), temp[i]);
          // timers[i]->Stop();
        }
      
      // values = temp.Last();
      values.AddSize(ir.Size(), Dimension()) = Trans(temp.Last());
      // t2.Stop();
      */
    }



    void Evaluate (const BaseMappedIntegrationRule & ir,
                   BareSliceMatrix<AutoDiff<1,double>> values) const override
    {
      if(compiled_function_deriv)
        {
          compiled_function_deriv(ir, values);
          return;
        }

      T_Evaluate (ir, Trans(values));
      return;
      
      /*
      typedef AutoDiff<1,double> T;
      ArrayMem<T,500> hmem(ir.Size()*totdim);      
      int mem_ptr = 0;
      ArrayMem<BareSliceMatrix<T,ColMajor>,100> temp(steps.Size());
      ArrayMem<BareSliceMatrix<T,ColMajor>,100> in(max_inputsize);

      for (int i = 0; i < steps.Size(); i++)
        {
          new (&temp[i]) BareSliceMatrix<T,ColMajor> (dim[i], &hmem[mem_ptr], DummySize(dim[i], ir.Size()));
          mem_ptr += ir.Size()*dim[i];
        }

      for (int i = 0; i < steps.Size(); i++)
        {
          auto inputi = inputs[i];
          for (int nr : Range(inputi))
            new (&in[nr]) BareSliceMatrix<T,ColMajor> (temp[inputi[nr]]);
          steps[i] -> Evaluate (ir, in.Range(0, inputi.Size()), temp[i]);
        }
      
      values.AddSize(ir.Size(), Dimension()) = Trans(temp.Last());
      */
    }



    void Evaluate (const BaseMappedIntegrationRule & ir,
                   BareSliceMatrix<AutoDiffDiff<1,double>> values) const override
    {
      if(compiled_function_dderiv)
      {
        compiled_function_dderiv(ir, values);
        return;
      }

      T_Evaluate (ir, Trans(values));
      return;

      /*
      typedef AutoDiffDiff<1,double> T;
      ArrayMem<T,500> hmem(ir.Size()*totdim);      
      int mem_ptr = 0;
      ArrayMem<BareSliceMatrix<T,ColMajor>,100> temp(steps.Size());
      ArrayMem<BareSliceMatrix<T,ColMajor>,100> in(max_inputsize);

      for (int i = 0; i < steps.Size(); i++)
        {
          new (&temp[i]) BareSliceMatrix<T,ColMajor> (dim[i], &hmem[mem_ptr], DummySize(dim[i], ir.Size()));
          mem_ptr += ir.Size()*dim[i];
        }

      for (int i = 0; i < steps.Size(); i++)
        {
          auto inputi = inputs[i];
          for (int nr : Range(inputi))
            new (&in[nr]) BareSliceMatrix<T,ColMajor> (temp[inputi[nr]]);
          steps[i] -> Evaluate (ir, in.Range(0, inputi.Size()), temp[i]);
        }
      
      values.AddSize(ir.Size(), Dimension()) = Trans(temp.Last());
      */
    }


    
    void Evaluate (const SIMD_BaseMappedIntegrationRule & ir,
                   BareSliceMatrix<AutoDiff<1,SIMD<double>>> values) const override
    {
      if(compiled_function_simd_deriv)
        {
          compiled_function_simd_deriv(ir, values);
          return;
        }

      T_Evaluate (ir, values);
      return;

      /*
      typedef AutoDiff<1,SIMD<double>> T;
      // STACK_ARRAY(T, hmem, ir.Size()*totdim);
      ArrayMem<T,500> hmem(ir.Size()*totdim);
      size_t mem_ptr = 0;

      ArrayMem<BareSliceMatrix<T>,100> temp(steps.Size());
      ArrayMem<BareSliceMatrix<T>,100> in(max_inputsize);

      for (size_t i = 0; i < steps.Size()-1; i++)
        {
          new (&temp[i]) BareSliceMatrix<T> (ir.Size(), &hmem[mem_ptr], DummySize(dim[i], ir.Size()));
          mem_ptr += ir.Size()*dim[i];
        }
      // the final step goes to result matrix
      new (&temp.Last()) BareSliceMatrix<T>(values);
      
      for (size_t i = 0; i < steps.Size(); i++)
        {
          auto inputi = inputs[i];
          for (size_t nr : Range(inputi))
            new (&in[nr]) BareSliceMatrix<T> (temp[inputi[nr]]);
          steps[i] -> Evaluate (ir, in.Range(0, inputi.Size()), temp[i]);
        }
      */
    }


    
    void Evaluate (const SIMD_BaseMappedIntegrationRule & ir,
                   BareSliceMatrix<AutoDiffDiff<1,SIMD<double>>> values) const override
    {
      if(compiled_function_simd_dderiv)
      {
        compiled_function_simd_dderiv(ir, values);
        return;
      }
      
      T_Evaluate (ir, values);
      return;

      /*
      typedef AutoDiffDiff<1,SIMD<double>> T;
      // STACK_ARRAY(T, hmem, ir.Size()*totdim);
      ArrayMem<T,500> hmem(ir.Size()*totdim);      
      int mem_ptr = 0;
      ArrayMem<BareSliceMatrix<T>,100> temp(steps.Size());
      ArrayMem<BareSliceMatrix<T>,100> in(max_inputsize);

      for (int i = 0; i < steps.Size(); i++)
        {
          new (&temp[i]) BareSliceMatrix<T> (ir.Size(), &hmem[mem_ptr], DummySize(dim[i], ir.Size()));
          mem_ptr += ir.Size()*dim[i];
        }

      for (int i = 0; i < steps.Size(); i++)
        {
          auto inputi = inputs[i];
          for (int nr : Range(inputi))
            new (&in[nr]) BareSliceMatrix<T> (temp[inputi[nr]]);
          // in[nr] = &temp[inputi[nr]];

          steps[i] -> Evaluate (ir, in.Range(0, inputi.Size()), temp[i]);
        }

      values.AddSize(Dimension(), ir.Size()) = temp.Last();
      */
    }

    
    void Evaluate (const SIMD_BaseMappedIntegrationRule & ir,
                   BareSliceMatrix<SIMD<double>> values) const override
    {
      if(compiled_function_simd)
      {
        compiled_function_simd(ir, values);
        return;
      }

      T_Evaluate (ir, values);
      return;

      /*
      // STACK_ARRAY(SIMD<double>, hmem, ir.Size()*totdim);
      ArrayMem<SIMD<double>,500> hmem(ir.Size()*totdim);            
      int mem_ptr = 0;
      ArrayMem<BareSliceMatrix<SIMD<double>>,100> temp(steps.Size());
      ArrayMem<BareSliceMatrix<SIMD<double>>,100> in(max_inputsize);

      for (int i = 0; i < steps.Size(); i++)
        {
          new (&temp[i]) BareSliceMatrix<SIMD<double>> (ir.Size(), &hmem[mem_ptr], DummySize(dim[i], ir.Size()));
          mem_ptr += ir.Size()*dim[i];
        }

      for (int i = 0; i < steps.Size(); i++)
        {
          // timers[i]->Start();          
          auto inputi = inputs[i];
          for (int nr : Range(inputi))
            new (&in[nr]) BareSliceMatrix<SIMD<double>> (temp[inputi[nr]]);            
          // in[nr] = &temp[inputi[nr]];

          steps[i] -> Evaluate (ir, in.Range(0, inputi.Size()), temp[i]);
          // timers[i]->Stop();                    
        }

      BareSliceMatrix<SIMD<double>> temp_last = temp.Last();
      values.AddSize(Dimension(), ir.Size()) = temp_last;
      */
    }

    void Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<Complex> values) const override
    {
      if(compiled_function_complex)
      {
          compiled_function_complex(ir,values);
          return;
      }
      else
      {
          cf->Evaluate (ir, values);
      }
    }

    void Evaluate (const SIMD_BaseMappedIntegrationRule & ir,
                   BareSliceMatrix<SIMD<Complex>> values) const override
    {
      if(compiled_function_simd_complex)
      {
        compiled_function_simd_complex(ir,values);
        return;
      }
      else
      {
          cf->Evaluate (ir, values);
      }
    }

#ifdef OLD
    [[deprecated]]
    virtual void EvaluateDeriv (const BaseMappedIntegrationRule & ir,
                                FlatMatrix<double> values, FlatMatrix<double> deriv) const
    {
      /*
      if(compiled_function_deriv)
      {
        compiled_function_deriv(ir, values, deriv);
        return;
      }
      */
      
      /*
      Array<Matrix<>*> temp;
      Array<Matrix<>*> dtemp;
      for (int i = 0; i < steps.Size(); i++)
        {
          // timers[i]->Start();
          
          temp.Append (new Matrix<>(ir.Size(), dim[i]));
          dtemp.Append (new Matrix<>(ir.Size(), dim[i]));
          
          Array<FlatMatrix<>*> in;
          for (int j : inputs[i])
            in.Append (temp[j]);
          Array<FlatMatrix<>*> din;
          for (int j : inputs[i])
            din.Append (dtemp[j]);
          
          steps[i] -> EvaluateDeriv (ir, in, din, *temp[i], *dtemp[i]);
          // timers[i]->Stop();
        }

      values = *temp.Last();
      deriv = *dtemp.Last();
      
      for (int i = 0; i < steps.Size(); i++)
        delete temp[i];
      for (int i = 0; i < steps.Size(); i++)
        delete dtemp[i];
      */

      // int totdim = 0;
      // for (int d : dim) totdim += d;
      ArrayMem<double, 10000> hmem(ir.Size()*3*totdim);
      int mem_ptr = 0;
      
      ArrayMem<FlatMatrix<>,100> temp(steps.Size());
      ArrayMem<FlatMatrix<>,100> dtemp(steps.Size());
      ArrayMem<FlatMatrix<>*, 20> in, din;

      for (int i = 0; i < steps.Size(); i++)
        {
          // timers[i]->Start();
          temp[i].AssignMemory(ir.Size(), dim[i], &hmem[mem_ptr]);
          mem_ptr += ir.Size()*dim[i];
          dtemp[i].AssignMemory(ir.Size(), dim[i], &hmem[mem_ptr]);          
          mem_ptr += ir.Size()*dim[i];

          in.SetSize(0);
          din.SetSize(0);
          for (int j : inputs[i])
            in.Append (&temp[j]);
          for (int j : inputs[i])
            din.Append (&dtemp[j]);
          steps[i] -> EvaluateDeriv (ir, in, din, temp[i], dtemp[i]);
          // timers[i]->Stop();
        }

      values = temp.Last();
      deriv = dtemp.Last();
    }

    [[deprecated]]
    virtual void EvaluateDDeriv (const BaseMappedIntegrationRule & ir,
                                 FlatMatrix<double> values, FlatMatrix<double> deriv,
                                 FlatMatrix<double> dderiv) const
    {
      /*
      if(compiled_function_dderiv)
      {
        compiled_function_dderiv(ir, values, deriv, dderiv);
        return;
      }
      */
      int totdim = 0;
      for (int d : dim) totdim += d;
      ArrayMem<double, 10000> hmem(ir.Size()*3*totdim);
      int mem_ptr = 0;
      
      Array<FlatMatrix<>> temp(steps.Size());
      Array<FlatMatrix<>> dtemp(steps.Size());
      Array<FlatMatrix<>> ddtemp(steps.Size());
      ArrayMem<FlatMatrix<>*, 20> in, din, ddin;

      for (int i = 0; i < steps.Size(); i++)
        {
          // timers[i]->Start();          
          temp[i].AssignMemory(ir.Size(), dim[i], &hmem[mem_ptr]);
          mem_ptr += ir.Size()*dim[i];
          dtemp[i].AssignMemory(ir.Size(), dim[i], &hmem[mem_ptr]);          
          mem_ptr += ir.Size()*dim[i];
          ddtemp[i].AssignMemory(ir.Size(), dim[i], &hmem[mem_ptr]);          
          mem_ptr += ir.Size()*dim[i];

          in.SetSize(0);
          din.SetSize(0);
          ddin.SetSize(0);
          for (int j : inputs[i])
            in.Append (&temp[j]);
          for (int j : inputs[i])
            din.Append (&dtemp[j]);
          for (int j : inputs[i])
            ddin.Append (&ddtemp[j]);

          steps[i] -> EvaluateDDeriv (ir, in, din, ddin, temp[i], dtemp[i], ddtemp[i]);
          // timers[i]->Stop();                    
        }

      values = temp.Last();
      deriv = dtemp.Last();
      dderiv = ddtemp.Last();
    }
#endif

    
#ifdef OLD
    virtual void EvaluateDeriv (const SIMD_BaseMappedIntegrationRule & ir, 
                                AFlatMatrix<double> values, AFlatMatrix<double> deriv) const
    {
      /*
      if(compiled_function_simd_deriv)
      {
        compiled_function_simd_deriv(ir, values, deriv);
        return;
      }
      */
      // throw ExceptionNOSIMD ("*************** CompiledCF :: EvaluateDeriv not available without codegeneration");


      STACK_ARRAY(SIMD<double>, hmem, 2*ir.Size()*totdim);      
      int mem_ptr = 0;
      ArrayMem<AFlatMatrix<double>,100> temp(steps.Size());
      ArrayMem<AFlatMatrix<double>,100> dtemp(steps.Size());
      ArrayMem<AFlatMatrix<double>*,100> in(max_inputsize), din(max_inputsize);

      for (int i = 0; i < steps.Size(); i++)
        {
          new (&temp[i]) AFlatMatrix<double> (dim[i], ir.IR().GetNIP(), &hmem[mem_ptr]);
          mem_ptr += ir.Size()*dim[i];
          new (&dtemp[i]) AFlatMatrix<double> (dim[i], ir.IR().GetNIP(), &hmem[mem_ptr]);
          mem_ptr += ir.Size()*dim[i];
        }

      for (int i = 0; i < steps.Size(); i++)
        {
          // timers[i]->Start();          
          auto inputi = inputs[i];
          for (int nr : Range(inputi))
            {
              in[nr] = &temp[inputi[nr]];
              din[nr] = &dtemp[inputi[nr]];
            }

          steps[i] -> EvaluateDeriv (ir, in.Range(0, inputi.Size()), din.Range(0, inputi.Size()),
                                     temp[i], dtemp[i]);

          // timers[i]->Stop();                    
        }
      values = temp.Last();
      deriv = dtemp.Last();
    }

    virtual void EvaluateDDeriv (const SIMD_BaseMappedIntegrationRule & ir, 
                                 AFlatMatrix<double> values, AFlatMatrix<double> deriv,
                                 AFlatMatrix<double> dderiv) const
    {
      /*
      if(compiled_function_simd_dderiv)
      {
        compiled_function_simd_dderiv(ir, values, deriv, dderiv);
        return;
      }
      */
      // throw ExceptionNOSIMD ("*************** CompiledCF :: EvaluateDDeriv coming soon");
      STACK_ARRAY(SIMD<double>, hmem, 3*ir.Size()*totdim);      
      int mem_ptr = 0;
      ArrayMem<AFlatMatrix<double>,100> temp(steps.Size());
      ArrayMem<AFlatMatrix<double>,100> dtemp(steps.Size());
      ArrayMem<AFlatMatrix<double>,100> ddtemp(steps.Size());
      ArrayMem<AFlatMatrix<double>*,100> in(max_inputsize), din(max_inputsize), ddin(max_inputsize);

      for (int i = 0; i < steps.Size(); i++)
        {
          new (&temp[i]) AFlatMatrix<double> (dim[i], ir.IR().GetNIP(), &hmem[mem_ptr]);
          mem_ptr += ir.Size()*dim[i];
          new (&dtemp[i]) AFlatMatrix<double> (dim[i], ir.IR().GetNIP(), &hmem[mem_ptr]);
          mem_ptr += ir.Size()*dim[i];
          new (&ddtemp[i]) AFlatMatrix<double> (dim[i], ir.IR().GetNIP(), &hmem[mem_ptr]);
          mem_ptr += ir.Size()*dim[i];
        }

      for (int i = 0; i < steps.Size(); i++)
        {
          // timers[i]->Start();          
          auto inputi = inputs[i];
          for (int nr : Range(inputi))
            {
              in[nr] = &temp[inputi[nr]];
              din[nr] = &dtemp[inputi[nr]];
              ddin[nr] = &ddtemp[inputi[nr]];
            }

          steps[i] -> EvaluateDDeriv (ir, in.Range(0, inputi.Size()), din.Range(0, inputi.Size()),
                                      ddin.Range(0, inputi.Size()),
                                      temp[i], dtemp[i], ddtemp[i]);
          // timers[i]->Stop();                    
        }
      values = temp.Last();
      deriv = dtemp.Last();
      dderiv = ddtemp.Last();
    }
#endif
    
    void GenerateCode(Code &code, FlatArray<int> inputs, int index) const override
    {
      return cf->GenerateCode(code, inputs, index);
    }
  };

class RealCF : public CoefficientFunctionNoDerivative
  {
    shared_ptr<CoefficientFunction> cf;
    bool cf_is_complex;
  public:
    RealCF() = default;
    RealCF(shared_ptr<CoefficientFunction> _cf)
      : CoefficientFunctionNoDerivative(_cf->Dimension(),false), cf(_cf)
    {
      cf_is_complex = cf->IsComplex();
    }

    void DoArchive(Archive& ar) override
    {
      CoefficientFunctionNoDerivative::DoArchive(ar);
      ar.Shallow(cf) & cf_is_complex;
    }

    virtual string GetDescription() const override
    {
      return "RealCF";
    }
      using CoefficientFunctionNoDerivative::Evaluate;
    virtual double Evaluate(const BaseMappedIntegrationPoint& ip) const override
    {
      if(cf->IsComplex())
        {
          Vec<1,Complex> val;
          cf->Evaluate(ip,val);
          return val(0).real();
        }
      return cf->Evaluate(ip);
    }

    virtual void Evaluate(const BaseMappedIntegrationPoint& ip, FlatVector<> vec) const override
    {
      if(cf->IsComplex())
        {
          VectorMem<10,Complex> complex_vec(vec.Size());
          cf->Evaluate(ip,complex_vec);
          vec = Real(complex_vec);
        }
      else
          cf->Evaluate(ip,vec);
    }

    virtual void Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<double> values) const override
    {
      if (!cf_is_complex)
        {
          cf->Evaluate(ir, values);
          return;
        }

      STACK_ARRAY(Complex, mem, ir.Size()*Dimension());
      FlatMatrix<Complex> cvalues(ir.Size(), Dimension(), &mem[0]);
      cf->Evaluate (ir, cvalues);
      values.AddSize(ir.Size(), Dimension()) = Real(cvalues);
    }

    virtual void Evaluate (const SIMD_BaseMappedIntegrationRule & ir,
                           BareSliceMatrix<SIMD<double>> values) const override
    {
      if (!cf_is_complex)
        {
          cf->Evaluate(ir, values);
          return;
        }

      STACK_ARRAY(SIMD<Complex>, mem, ir.Size()*Dimension());
      FlatMatrix<SIMD<Complex>> cvalues(Dimension(), ir.Size(), &mem[0]);
      cf->Evaluate (ir, cvalues);
      values.AddSize(Dimension(), ir.Size()) = Real(cvalues);
    }
  };

  class ImagCF : public CoefficientFunctionNoDerivative
  {
    shared_ptr<CoefficientFunction> cf;
  public:
    ImagCF() = default;
    ImagCF(shared_ptr<CoefficientFunction> _cf) : CoefficientFunctionNoDerivative(_cf->Dimension(),false), cf(_cf)
    { ; }

    void DoArchive(Archive& ar) override
    {
      CoefficientFunctionNoDerivative::DoArchive(ar);
      ar.Shallow(cf);
    }

    virtual string GetDescription() const override
    {
      return "ImagCF";
    }
    using CoefficientFunctionNoDerivative::Evaluate;
    virtual double Evaluate(const BaseMappedIntegrationPoint& ip) const override
    {
      if(!cf->IsComplex())
          throw Exception("real cf has no imag part!");

      VectorMem<10,Complex> val(cf->Dimension());
      cf->Evaluate(ip,val);
      return val(0).imag();
    }

    virtual void Evaluate(const BaseMappedIntegrationPoint& ip, FlatVector<> vec) const override
    {
      if(cf->IsComplex())
        {
          VectorMem<10,Complex> complex_vec(vec.Size());
          cf->Evaluate(ip,complex_vec);
          vec = Imag(complex_vec);
        }
      else
          cf->Evaluate(ip,vec);
    }

    virtual void Evaluate (const BaseMappedIntegrationRule & ir, BareSliceMatrix<double> values) const override
    {
      if (cf->IsComplex())
        {
          STACK_ARRAY(Complex, mem, ir.Size()*Dimension());
          FlatMatrix<Complex> cvalues(ir.Size(), Dimension(), &mem[0]);
          cf->Evaluate (ir, cvalues);
          values.AddSize(ir.Size(),Dimension()) = Imag(cvalues);
        }
      else
        values.AddSize(ir.Size(),Dimension()) = 0.;
    }

    virtual void Evaluate (const SIMD_BaseMappedIntegrationRule & ir,
                           BareSliceMatrix<SIMD<double>> values) const override
    {
      if (!cf->IsComplex())
          throw Exception("real cf has no imag part!");

      STACK_ARRAY(SIMD<Complex>, mem, ir.Size()*Dimension());
      FlatMatrix<SIMD<Complex>> cvalues(Dimension(), ir.Size(), &mem[0]);
      cf->Evaluate (ir, cvalues);
      values.AddSize(Dimension(), ir.Size()) = Imag(cvalues);
    }
  };

  shared_ptr<CoefficientFunction> Real(shared_ptr<CoefficientFunction> cf)
  {
    return make_shared<RealCF>(cf);
  }
  shared_ptr<CoefficientFunction> Imag(shared_ptr<CoefficientFunction> cf)
  {
    return make_shared<ImagCF>(cf);
  }

  shared_ptr<CoefficientFunction> Compile (shared_ptr<CoefficientFunction> c, bool realcompile, int maxderiv, bool wait)
  {
    auto cf = make_shared<CompiledCoefficientFunction> (c);
    if(realcompile)
      cf->RealCompile(maxderiv, wait);
    return cf;
  }

static RegisterClassForArchive<CoefficientFunction> regcf;
static RegisterClassForArchive<ConstantCoefficientFunction, CoefficientFunction> regccf;
static RegisterClassForArchive<ConstantCoefficientFunctionC, CoefficientFunction> regccfc;
static RegisterClassForArchive<ParameterCoefficientFunction, CoefficientFunction> regpar;
static RegisterClassForArchive<DomainConstantCoefficientFunction, CoefficientFunction> regdccf;
static RegisterClassForArchive<DomainVariableCoefficientFunction, CoefficientFunction> regdvcf;
static RegisterClassForArchive<IntegrationPointCoefficientFunction, CoefficientFunction> regipcf;
static RegisterClassForArchive<PolynomialCoefficientFunction, CoefficientFunction> regpolcf;
static RegisterClassForArchive<FileCoefficientFunction, CoefficientFunction> regfilecf;
static RegisterClassForArchive<CoordCoefficientFunction, CoefficientFunction> regcoocf;
static RegisterClassForArchive<DomainWiseCoefficientFunction, CoefficientFunction> regdwcf;
static RegisterClassForArchive<VectorialCoefficientFunction, CoefficientFunction> regveccf;
static RegisterClassForArchive<ComponentCoefficientFunction, CoefficientFunction> regcompcf;
static RegisterClassForArchive<ScaleCoefficientFunction, CoefficientFunction> regscale;
static RegisterClassForArchive<ScaleCoefficientFunctionC, CoefficientFunction> regscalec;
static RegisterClassForArchive<MultScalVecCoefficientFunction, CoefficientFunction> regscalvec;
static RegisterClassForArchive<MultVecVecCoefficientFunction, CoefficientFunction> regmultvecvec;
static RegisterClassForArchive<T_MultVecVecCoefficientFunction<1>, CoefficientFunction> regtmultvecvec1;
static RegisterClassForArchive<T_MultVecVecCoefficientFunction<2>, CoefficientFunction> regtmultvecvec2;
static RegisterClassForArchive<T_MultVecVecCoefficientFunction<3>, CoefficientFunction> regtmultvecvec3;
static RegisterClassForArchive<EigCoefficientFunction, CoefficientFunction> regeigcf;
static RegisterClassForArchive<NormCoefficientFunction, CoefficientFunction> regnormcf;
static RegisterClassForArchive<NormCoefficientFunctionC, CoefficientFunction> regnormcfc;
static RegisterClassForArchive<MultMatMatCoefficientFunction, CoefficientFunction> regmultmatmatcf;
static RegisterClassForArchive<MultMatVecCoefficientFunction, CoefficientFunction> regmultmatveccf;
static RegisterClassForArchive<TransposeCoefficientFunction, CoefficientFunction> regtransposecf;
static RegisterClassForArchive<SymmetricCoefficientFunction, CoefficientFunction> regsymmetriccf;
static RegisterClassForArchive<InverseCoefficientFunction<1>, CoefficientFunction> reginversecf1;
static RegisterClassForArchive<InverseCoefficientFunction<2>, CoefficientFunction> reginversecf2;
static RegisterClassForArchive<InverseCoefficientFunction<3>, CoefficientFunction> reginversecf3;
static RegisterClassForArchive<DeterminantCoefficientFunction<1>, CoefficientFunction> regdetcf1;
static RegisterClassForArchive<DeterminantCoefficientFunction<2>, CoefficientFunction> regdetcf2;
static RegisterClassForArchive<DeterminantCoefficientFunction<3>, CoefficientFunction> regdetcf3;
static RegisterClassForArchive<cl_BinaryOpCF<GenericPlus>, CoefficientFunction> regcfplus;
static RegisterClassForArchive<cl_BinaryOpCF<GenericMinus>, CoefficientFunction> regcfminus;
static RegisterClassForArchive<cl_BinaryOpCF<GenericMult>, CoefficientFunction> regcfmult;
static RegisterClassForArchive<cl_BinaryOpCF<GenericDiv>, CoefficientFunction> regcfdiv;
static RegisterClassForArchive<IfPosCoefficientFunction, CoefficientFunction> regfifpos;
static RegisterClassForArchive<RealCF, CoefficientFunction> regrealcf;
static RegisterClassForArchive<ImagCF, CoefficientFunction> regimagcf;
static RegisterClassForArchive<CompiledCoefficientFunction, CoefficientFunction> regcompiledcf;
static RegisterClassForArchive<OtherCoefficientFunction, CoefficientFunction> regothercf;
}

