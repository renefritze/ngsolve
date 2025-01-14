#ifndef FILE_NGS_PARALLELVECTOR
#define FILE_NGS_PARALLELVECTOR

/* ************************************************************************/
/* File:   parallelvector.hpp                                             */
/* Author: Astrid Sinwel, Joachim Schoeberl                               */
/* Date:   2007,2011                                                      */
/* ************************************************************************/



// #ifdef PARALLEL

namespace ngla
{
  // using ngparallel::ParallelDofs;
  // using ngla::ParallelDofs;


  class NGS_DLL_HEADER ParallelBaseVector : virtual public BaseVector
  {
  protected:
    mutable PARALLEL_STATUS status;
    shared_ptr<ParallelDofs> paralleldofs;    
    shared_ptr<BaseVector> local_vec;
    
  public:
    ParallelBaseVector ()
    { ; }

    template <typename T> 
    BaseVector & operator= (const VVecExpr<T> & v)
    {
      v.AssignTo (1.0, *this);
      return *this;
    }

    virtual PARALLEL_STATUS Status () const { return status; }

    virtual void SetStatus ( PARALLEL_STATUS astatus ) const
    {
      status = astatus;
    }

    virtual PARALLEL_STATUS GetParallelStatus () const { return Status(); }
    virtual void SetParallelStatus (PARALLEL_STATUS stat) const { SetStatus (stat); }



    virtual shared_ptr<ParallelDofs> GetParallelDofs () const
    {
      return paralleldofs; 
    }
    
    virtual bool IsParallelVector () const
    {
      return (this->Status() != NOT_PARALLEL);
    }
    
    virtual BaseVector & SetScalar (double scal);
    virtual BaseVector & SetScalar (Complex scal);
    
    virtual BaseVector & Set (double scal, const BaseVector & v);
    virtual BaseVector & Set (Complex scal, const BaseVector & v);

    virtual BaseVector & Add (double scal, const BaseVector & v);
    virtual BaseVector & Add (Complex scal, const BaseVector & v);

    void PrintStatus ( ostream & ost ) const;

    virtual shared_ptr<BaseVector> GetLocalVector () const
    { return local_vec; }
    
    virtual void Cumulate () const; 
    
    virtual void Distribute() const = 0;
    // { cerr << "ERROR -- Distribute called for BaseVector, is not parallel" << endl; }
    
    virtual void ISend ( int dest, MPI_Request & request ) const;
    // virtual void Send ( int dest ) const;
    
    virtual void IRecvVec ( int dest, MPI_Request & request ) = 0;
    // { cerr << "ERROR -- IRecvVec called for BaseVector, is not parallel" << endl; }

    // virtual void RecvVec ( int dest )
    // { cerr << "ERROR -- IRecvVec called for BaseVector, is not parallel" << endl; }
    
    virtual void AddRecvValues( int sender ) = 0;
    // { cerr << "ERROR -- AddRecvValues called for BaseVector, is not parallel" << endl; }

    virtual void SetParallelDofs (shared_ptr<ParallelDofs> aparalleldofs, 
				  const Array<int> * procs = 0) = 0;
    /*
    { 
      if ( aparalleldofs == 0 ) return;
      cerr << "ERROR -- SetParallelDofs called for BaseVector, is not parallel" << endl; 
    }
    */
  };





  inline ParallelBaseVector * dynamic_cast_ParallelBaseVector (BaseVector * x)
  {
    // cout << "my dynamic * cast" << endl;
    AutoVector * ax = dynamic_cast<AutoVector*> (x);
    if (ax)
      return dynamic_cast<ParallelBaseVector*> (&**ax);
    return dynamic_cast<ParallelBaseVector*> (x);
  }

  inline const ParallelBaseVector * dynamic_cast_ParallelBaseVector (const BaseVector * x)
  {
    // cout << "my dynamic const * cast" << endl;
    const AutoVector * ax = dynamic_cast<const AutoVector*> (x);
    if (ax)
      { 
        // cout << "is an autovector" << endl; 
        return dynamic_cast<const ParallelBaseVector*> (&**ax);
      }
    return dynamic_cast<const ParallelBaseVector*> (x);
  }
  
  inline ParallelBaseVector & dynamic_cast_ParallelBaseVector (BaseVector & x)
  {
    // cout << "my dynamic cast" << endl;
    AutoVector * ax = dynamic_cast<AutoVector*> (&x);
    if (ax)
      return dynamic_cast<ParallelBaseVector&> (**ax);
    return dynamic_cast<ParallelBaseVector&> (x);
  }
  inline const ParallelBaseVector & dynamic_cast_ParallelBaseVector (const BaseVector & x)
  {
    // cout << "my dynamic cast" << endl;
    const AutoVector * ax = dynamic_cast<const AutoVector*> (&x);
    if (ax)
      return dynamic_cast<const ParallelBaseVector&> (**ax);
    return dynamic_cast<const ParallelBaseVector&> (x);
  }






  
  template <class SCAL>
  class NGS_DLL_HEADER S_ParallelBaseVector 
    : virtual public S_BaseVector<SCAL>, 
      virtual public ParallelBaseVector
  {
  protected:
    virtual SCAL InnerProduct (const BaseVector & v2, bool conjugate = false) const;
    virtual BaseVector & SetScalar (double scal)
    { return ParallelBaseVector::SetScalar(scal); }
  };


  template <class SCAL>
  class NGS_DLL_HEADER S_ParallelBaseVectorPtr
    : virtual public S_BaseVectorPtr<SCAL>, 
      virtual public S_ParallelBaseVector<SCAL>
  {
  protected:
    typedef SCAL TSCAL;
    using ParallelBaseVector :: status;
    using ParallelBaseVector :: paralleldofs;

    Table<SCAL> * recvvalues;

    using S_BaseVectorPtr<TSCAL> :: pdata;
    using ParallelBaseVector :: local_vec;

  public:
    // S_ParallelBaseVectorPtr (int as, int aes, void * adata) throw();
    S_ParallelBaseVectorPtr (int as, int aes, shared_ptr<ParallelDofs> apd, PARALLEL_STATUS stat) throw();

    virtual ~S_ParallelBaseVectorPtr ();
    virtual void SetParallelDofs (shared_ptr<ParallelDofs> aparalleldofs, const Array<int> * procs=0 );

    virtual void Distribute() const;
    virtual ostream & Print (ostream & ost) const;

    virtual void  IRecvVec ( int dest, MPI_Request & request );
    // virtual void  RecvVec ( int dest );
    virtual void AddRecvValues( int sender );
    virtual AutoVector CreateVector () const;

    virtual double L2Norm () const;
  };
 



  template <typename T = double>
  class ParallelVVector : public VVector<T>, 
			  public S_ParallelBaseVectorPtr<typename mat_traits<T>::TSCAL>
  {
    typedef typename mat_traits<T>::TSCAL TSCAL;
    enum { ES = sizeof(T) / sizeof(TSCAL) };

    using S_BaseVectorPtr<TSCAL> :: pdata;
    using ParallelBaseVector :: local_vec;

  public:
    explicit ParallelVVector (int as, shared_ptr<ParallelDofs> aparalleldofs,
			      PARALLEL_STATUS astatus = CUMULATED)
      : S_BaseVectorPtr<TSCAL> (as, ES), VVector<T> (as), 
	S_ParallelBaseVectorPtr<TSCAL> (as, ES, aparalleldofs, astatus)
    { local_vec = make_shared<VFlatVector<T>>(as, (T*)pdata); }

    explicit ParallelVVector (shared_ptr<ParallelDofs> aparalleldofs,
			      PARALLEL_STATUS astatus = CUMULATED)
      : S_BaseVectorPtr<TSCAL> (aparalleldofs->GetNDofLocal(), ES), 
	VVector<T> (aparalleldofs->GetNDofLocal()), 
	S_ParallelBaseVectorPtr<TSCAL> (aparalleldofs->GetNDofLocal(), ES, aparalleldofs, astatus)
    { local_vec = make_shared<VFlatVector<T>>(aparalleldofs->GetNDofLocal(), (T*)pdata); }


    virtual ~ParallelVVector() throw()
    { ; }
  };


  template <typename T = double>
  class ParallelVFlatVector : public VFlatVector<T>,
			      public S_ParallelBaseVectorPtr<typename mat_traits<T>::TSCAL>
  {
    typedef typename mat_traits<T>::TSCAL TSCAL;
    enum { ES = sizeof(T) / sizeof(TSCAL) };

    using S_BaseVectorPtr<TSCAL> :: pdata;
    using ParallelBaseVector :: local_vec;

  public:
    explicit ParallelVFlatVector (int as, T * adata, 
				  shared_ptr<ParallelDofs> aparalleldofs, 
				  PARALLEL_STATUS astatus)
    : S_BaseVectorPtr<TSCAL> (as, ES, adata),
      VFlatVector<T> (as, adata),
      S_ParallelBaseVectorPtr<TSCAL> (as, ES, aparalleldofs, astatus)
    { local_vec = make_shared<VFlatVector<T>>(aparalleldofs->GetNDofLocal(), (T*)pdata); }

    explicit ParallelVFlatVector ()
    : S_BaseVectorPtr<TSCAL> (0, ES, NULL),
      S_ParallelBaseVectorPtr<TSCAL> (0, ES, NULL)
    { local_vec = make_shared<VFlatVector<T>>(0, NULL); }
      
    virtual ~ParallelVFlatVector() throw()
    { ; }
  };
}

// #endif
#endif
