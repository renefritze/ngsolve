#ifndef FILE_MPIWRAPPER
#define FILE_MPIWRAPPER

/* ************************************************************************/
/* File:   mpiwrapper.hpp                                                 */
/* Author: Joachim Schoeberl                                              */
/* Date:   2007,2011                                                      */
/* ************************************************************************/




#ifdef PARALLEL

/*
#undef SEEK_SET
#undef SEEK_CUR
#undef SEEK_END
*/

// #define OMPI_SKIP_MPICXX
// #include <mpi.h>

#endif

/*
namespace netgen
{
#ifndef PARALLEL
  typedef int MPI_Comm;
#endif
  extern DLL_HEADER MPI_Comm ng_comm;
  class PyMPI_Comm;
}
*/



namespace ngstd
{

#ifdef PARALLEL
  
  // extern MPI_Comm ngs_comm;

  enum { MPI_TAG_CMD = 110 };
  enum { MPI_TAG_SOLVE = 1110 };

  /*
  struct no_base_impl {};
  template <class T>
  class MPI_Traits : public no_base_impl
  {
  public:
    //    static MPI_Datatype MPIType () 
    // { 
    // cerr << "MPIType " << typeid(T).name() << " not available" << endl;
    // return 0; 
    // }
  };
  
  template <>
  class MPI_Traits<int>
  {
  public:
    static MPI_Datatype MPIType () { return MPI_INT; }
  };

  template <>
  class MPI_Traits<size_t>
  {
  public:
    static MPI_Datatype MPIType () { return MPI_UINT64_T; }
  };
    
  template <>
  class MPI_Traits<double>
  {
  public:
    static MPI_Datatype MPIType () { return MPI_DOUBLE; }
  };
  

  template <>
  class MPI_Traits<bool>
  {
  public:
    static MPI_Datatype MPIType () { return MPI_C_BOOL; }
  };

  template <>
  class MPI_Traits<short>
  {
  public:
    static MPI_Datatype MPIType () { return MPI_SHORT; }
  }; 

  template <>
  class MPI_Traits<unsigned char>
  {
  public:
    static MPI_Datatype MPIType () { return MPI_BYTE; }
  }; 
  */


  
  // #define NGSMPI_ENABLE_FOR_STD typename enable_if<!is_base_of<no_base_impl, MPI_Traits<T> >::value, int>::type = 0
#define NGSMPI_ENABLE_FOR_STD typename T2 = decltype(ngcore::GetMPIType<T>()) 
  /*
  template <class T, NGSMPI_ENABLE_FOR_STD>
  INLINE MPI_Datatype MyGetMPIType ()
  {
    return MPI_Traits<T>::MPIType();
  }
  */
  template <class T, class T2 = decltype(ngcore::GetMPIType<T>())>
  INLINE MPI_Datatype MyGetMPIType ()
  {
    return ngcore::GetMPIType<T>();
  }

  /*
  INLINE int MyMPI_GetNTasks (MPI_Comm comm)
  {
    static Timer t("dummy - size");
    RegionTimer r(t);

    int ntasks;
    MPI_Comm_size(comm, &ntasks);
    return ntasks;
  }
  
  INLINE int MyMPI_GetId (MPI_Comm comm)
  {
    static Timer t("dummy - rank");
    RegionTimer r(t);

    int id;
    MPI_Comm_rank(comm, &id);
    return id;
  }

  INLINE void MyMPI_Barrier (MPI_Comm comm)
  {
    static Timer t("dummy - barrier");
    RegionTimer r(t);

    MPI_Barrier (comm);
  }
  */

  /**
  template<typename T, typename enable_if<!is_base_of<no_base_impl, MPI_Traits<T> >::value, int>::type = 0 >
  INLINE void MyMPI_Send( T & s, int dest, int tag = MPI_TAG_SOLVE, MPI_Comm comm = ngs_comm)
  { MPI_Send (&val, 1, MyGetMPIType<T>(), dest, tag, comm); }
  **/


  class NgsMPI_Comm : public NgMPI_Comm
  {
  public:
    using NgMPI_Comm::NgMPI_Comm;
    NgsMPI_Comm (const NgMPI_Comm & c) : NgMPI_Comm(c) { ; }
    NgsMPI_Comm (NgMPI_Comm && c) : NgMPI_Comm(c) { ; }
    
    using NgMPI_Comm::Send;
    using NgMPI_Comm::Recv;
    
    template<typename T, typename T2 = decltype(GetMPIType<T>())>
    void Send(FlatArray<T> s, int dest, int tag) const {
      MPI_Send (&s[0], s.Size(), GetMPIType<T>(), dest, tag, comm);
    }
    
    template <typename T, typename T2 = decltype(GetMPIType<T>())>
    void Recv (FlatArray <T> s, int src, int tag) const {
      MPI_Recv (&s[0], s.Size(), GetMPIType<T> (), src, tag, comm, MPI_STATUS_IGNORE);
    }
    
    template <typename T, typename T2 = decltype(GetMPIType<T>())>
    void Recv (Array <T> & s, int src, int tag) const
    {
      MPI_Status status;
      int len;
      const MPI_Datatype MPI_T  = GetMPIType<T> ();
      MPI_Probe (src, tag, comm, &status);
      MPI_Get_count (&status, MPI_T, &len);
      s.SetSize (len);
      MPI_Recv (&s[0], len, MPI_T, src, tag, comm, MPI_STATUS_IGNORE);
    }
    
  };
  
  /** --- blocking P2P --- **/

#ifdef OLD
  template<typename T, typename T2 = decltype(MyGetMPIType<T>())> // , NGSMPI_ENABLE_FOR_STD>
  INLINE void MyMPI_Send( T & val, int dest, int tag /* = MPI_TAG_SOLVE */, MPI_Comm comm /* = ngs_comm*/)
  { MPI_Send (&val, 1, MyGetMPIType<T>(), dest, tag, comm); }
  template<typename T, typename T2 = decltype(MyGetMPIType<T>())> // , NGSMPI_ENABLE_FOR_STD>
  INLINE void MyMPI_Recv (T & val, int src, int tag /* = MPI_TAG_SOLVE */, MPI_Comm comm /* = ngs_comm*/)
  { MPI_Recv (&val, 1, MyGetMPIType<T>(), src, tag, comm, MPI_STATUS_IGNORE); }

  // template<typename T> // , NGSMPI_ENABLE_FOR_STD>
     // INLINE void MyMPI_Send(FlatArray<T> s, int dest, int tag /* = MPI_TAG_SOLVE */, MPI_Comm comm)
  // { MPI_Send( &s[0], s.Size(), MyGetMPIType<T>(), dest, tag, comm); }
  template <typename T> // , NGSMPI_ENABLE_FOR_STD>
  INLINE void MyMPI_Recv (FlatArray <T> s, int src, int tag /* = MPI_TAG_SOLVE */, MPI_Comm comm /* = ngs_comm */)
  { MPI_Recv (&s[0], s.Size(), MyGetMPIType<T> (), src, tag, comm, MPI_STATUS_IGNORE); }
  template <typename T> // , NGSMPI_ENABLE_FOR_STD>
  INLINE void MyMPI_Recv (Array <T> &s, int src, int tag /* = MPI_TAG_SOLVE */, MPI_Comm comm /* = ngs_comm */)
  {
    MPI_Status status;
    int len;
    const MPI_Datatype MPI_T  = MyGetMPIType<T> ();
    MPI_Probe (src, tag, comm, &status);
    MPI_Get_count (&status, MPI_T, &len);
    s.SetSize (len);
    MPI_Recv (&s[0], len, MPI_T, src, tag, comm, MPI_STATUS_IGNORE);
  }
#endif
  
  INLINE void MyMPI_SendCmd (const char * cmd, NgMPI_Comm comm)
  {
    int ntasks = comm.Size(); 
    if(ntasks==1) return;
    for (int dest = 1; dest < ntasks; dest++)
      MPI_Send( (void*)cmd, (strlen(cmd)+1), MPI_CHAR, dest, MPI_TAG_CMD, MPI_COMM_WORLD);
  }
  INLINE void MyMPI_Recv (string & s, int src, int tag /* = MPI_TAG_SOLVE */, MPI_Comm comm)
  {
    MPI_Status status;
    int len;
    MPI_Probe (src, tag, comm, &status);
    MPI_Get_count (&status, MPI_CHAR, &len);
    s.resize (len); 
    MPI_Recv(&s[0], len, MPI_CHAR, src, tag, comm, MPI_STATUS_IGNORE);
  }

  
  /** --- non-blocking P2P --- **/

  template<typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE MPI_Request MyMPI_ISend (T & val, int dest, int tag /* = MPI_TAG_SOLVE*/ , MPI_Comm comm)
  {
    MPI_Request request;
    MPI_Isend (&val, 1, MyGetMPIType<T>(), dest, tag, comm, &request);
    return request;
  }
  template<typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE MPI_Request MyMPI_IRecv (T & val, int dest, int tag /* = MPI_TAG_SOLVE */, MPI_Comm comm)
  {
    MPI_Request request;
    MPI_Irecv (&val, 1, MyGetMPIType<T>(), dest, tag, comm, &request);
    return request;
  }

  template <typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE MPI_Request MyMPI_ISend (const FlatArray<T> & s, int dest, int tag /* = MPI_TAG_SOLVE */, MPI_Comm comm)
  { 
    static Timer t("dummy - isend");
    RegionTimer r(t);

    MPI_Request request;
    MPI_Datatype MPI_T  = MyGetMPIType<T> ();
    MPI_Isend (&s[0], s.Size(), MPI_T, dest, tag, comm, &request);
    return request;
  }
  template <typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE MPI_Request  MyMPI_IRecv (const FlatArray<T> & s, int src, int tag /* = MPI_TAG_SOLVE*/, MPI_Comm comm)
  { 
    static Timer t("dummy - irecv");
    RegionTimer r(t);

    MPI_Request request;
    MPI_Datatype MPI_T = MyGetMPIType<T> ();
    MPI_Irecv (&s[0], s.Size(), MPI_T, src, tag, comm, &request);
    return request;
  }

  INLINE void MyMPI_WaitAll (FlatArray<MPI_Request> requests)
  {
    static Timer t("dummy - waitall"); RegionTimer r(t);
    
    if (!requests.Size()) return;
    MPI_Waitall (requests.Size(), &requests[0], MPI_STATUSES_IGNORE);
  }
  
  INLINE int MyMPI_WaitAny (FlatArray<MPI_Request> requests)
  {
    static Timer t("dummy - waitany"); RegionTimer r(t);

    int nr;
    MPI_Waitany (requests.Size(), &requests[0], &nr, MPI_STATUS_IGNORE);
    return nr;
  }

  
  /** --- collectives --- **/

#ifdef OLD
  template <typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE T MyMPI_Reduce (T d, const MPI_Op & op /* = MPI_SUM */, MPI_Comm comm /* = ngs_comm */, int root = 0)
  {
    static Timer t("dummy - AllReduce");
    RegionTimer r(t);

    T global_d;
    MPI_Reduce (&d, &global_d, 1, MyGetMPIType<T>(), op, root, comm);
    return global_d;
  }

  template <typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE T MyMPI_AllReduce (T d, const MPI_Op & op /* = MPI_SUM */, MPI_Comm comm /* = ngs_comm */)
  {
    static Timer t("dummy - AllReduce");
    RegionTimer r(t);

    T global_d;
    MPI_Allreduce ( &d, &global_d, 1, MyGetMPIType<T>(), op, comm);
    return global_d;
  }
#endif
  
  template <typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE void MyMPI_Gather (T d, FlatArray<T> recv, // = FlatArray<T>(0, NULL),
			    MPI_Comm comm /* = ngs_comm */, int root = 0)
  {
    static Timer t ("dummy - Gather");
    RegionTimer r(t);

    MPI_Gather( &d, 1, MyGetMPIType<T>(),
		recv.Size()?&recv[0]:NULL, 1, MyGetMPIType<T>(), root, comm);
  }

  template <typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE void MyMPI_AllGather (T d, FlatArray<T> recv, MPI_Comm comm /* = ngs_comm */)
  {
    static Timer t("dummy - AllGather");
    RegionTimer r(t);

    MPI_Allgather (&d, 1, MyGetMPIType<T>(), 
		   &recv[0], 1, MyGetMPIType<T>(), comm);
  }
  
  template <typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE void MyMPI_AllToAll (FlatArray<T> send, FlatArray<T> recv, MPI_Comm comm /* = ngs_comm */)
  {
    static Timer t("dummy - AlltoAll");
    RegionTimer r(t);

    MPI_Alltoall (&send[0], 1, MyGetMPIType<T>(), 
		  &recv[0], 1, MyGetMPIType<T>(), comm);
  }


#ifdef OLD
  template <typename T, NGSMPI_ENABLE_FOR_STD>
  INLINE void MyMPI_Bcast (T & s, MPI_Comm comm /* = ngs_comm*/, int root = 0)
  { MPI_Bcast (&s, 1, MyGetMPIType<T>(), root, comm); }

  INLINE void MyMPI_Bcast (string & s, MPI_Comm comm /* = ngs_comm */, int root = 0)
  {
    int len = s.length();
    MyMPI_Bcast (len, comm);
    if (MyMPI_GetId(comm) != 0) s.resize (len);
    MPI_Bcast (&s[0], len, MPI_CHAR, root, comm);
  }
#endif
  
  using ngcore::NgMPI_Comm;
  
  /*
  class NgMPI_Comm
  {
    MPI_Comm comm;
    int * refcount;
  public:
    NgMPI_Comm (MPI_Comm _comm, bool owns = false)
      : comm(_comm)
    {
      if (!owns)
        refcount = nullptr;
      else
        refcount = new int{1};
    }
    
    NgMPI_Comm (const NgMPI_Comm & c)
      : comm(c.comm), refcount(c.refcount)
    {
      if (refcount) (*refcount)++;
    }

    NgMPI_Comm (NgMPI_Comm && c)
      : comm(c.comm), refcount(c.refcount)
    {
      c.refcount = nullptr;
    }
    
    ~NgMPI_Comm()
    {
      if (refcount)
        if (--(*refcount) == 0)
          MPI_Comm_free(&comm);
    }
    
    operator MPI_Comm() const { return comm; }

    auto Rank() const { int r; MPI_Comm_rank(comm, &r); return r; }
    auto Size() const { int s; MPI_Comm_size(comm, &s); return s; }    
  };
  */


class MyMPI
{
  bool initialized_by_me;
public:
  MyMPI(int argc, char ** argv) 
  {
    int is_init = -1;
    MPI_Initialized(&is_init);
    if (!is_init)
      {
        MPI_Init (&argc, &argv);
        initialized_by_me = true;
      }
    else
      initialized_by_me = false;
      
    // MPI_Comm_dup ( MPI_COMM_WORLD, &ngs_comm);      
    // ngs_comm = MPI_COMM_WORLD;
    // netgen::ng_comm = ngs_comm;

    NgMPI_Comm comm(MPI_COMM_WORLD);
    NGSOStream::SetGlobalActive (comm.Rank() == 0);
    
    if (comm.Size() > 1)
      TaskManager::SetNumThreads (1);
  }

  ~MyMPI()
  {
    // MPI_Comm_free(& ngs_comm);
    if (initialized_by_me)
      MPI_Finalize ();
  }
};




  
#undef NGSMPI_ENABLE_FOR_STD
#else
  using ngcore::MPI_Comm;
  using ngcore::MPI_COMM_WORLD;

  // enum { MPI_COMM_WORLD = 12345, MPI_COMM_NULL = 0};

  enum { MPI_TAG_CMD = 110 };
  enum { MPI_TAG_SOLVE = 1110 };
  
  // typedef int MPI_Comm;
  typedef int MPI_Datatype;
  // typedef int MPI_Request;

  class NgsMPI_Comm : public NgMPI_Comm
  {
  public:
    using NgMPI_Comm::NgMPI_Comm;
    NgsMPI_Comm (const NgMPI_Comm & c) : NgMPI_Comm(c) { ; }
    NgsMPI_Comm (NgMPI_Comm && c) : NgMPI_Comm(c) { ; }
  };
  
  // typedef int MPI_Op;
  // enum { MPI_SUM = 0, MPI_MIN = 1, MPI_MAX = 2 };
  // INLINE int MyMPI_GetNTasks (MPI_Comm comm = MPI_COMM_WORLD) { return 1; }
  // INLINE int MyMPI_GetId (MPI_Comm comm = MPI_COMM_WORLD) { return 0; }

  // INLINE void MyMPI_Barrier (MPI_Comm comm) { ; }
  INLINE void MyMPI_SendCmd (const char * cmd, MPI_Comm comm) { ; }

  template <typename T>
  INLINE void MyMPI_Send (const T & data, int dest, int tag = 0)
  { ; }
  
  template <typename T>
  INLINE void MyMPI_Recv (const T & data, int dest, int tag = 0)
  { ; }

  // template <typename T>
  // INLINE T MyMPI_AllReduce (T d, int op, MPI_Comm comm)  { return d; }

  /*
  template <typename T>
  INLINE T MyMPI_Reduce (T d, int op, MPI_Comm comm) { return d; }

  template <class T>
  INLINE void MyMPI_Bcast (T & s, MPI_Comm comm) { ; }
  template <class T>
  INLINE void MyMPI_Bcast (Array<T> & s, MPI_Comm comm) { ; }
  INLINE void MyMPI_Bcast (string & s, MPI_Comm comm) { ; }
  */
  
  INLINE void MyMPI_WaitAll (FlatArray<MPI_Request> requests) { ; }

  class MyMPI
  {
  public:
    MyMPI(int argc, char ** argv) { ; }
  };

  enum { MPI_LOR = 4711 };
  // enum { MPI_SUM = 4711 };
  // enum { MPI_MAX = 4711 };
  // enum { MPI_MIN = 4711 };

  using ngcore::NgMPI_Comm;
  /*
  class NgMPI_Comm
  {
    MPI_Comm comm;
  public:
    NgMPI_Comm (MPI_Comm _comm) : comm(_comm) { ; }
    operator MPI_Comm() const { return comm; }

    auto Rank() const { return 0; }
    auto Size() const { return 1; }
  };
  */
#endif
  

  // using netgen::PyMPI_Comm;
}

#endif
