#ifndef FILE_HCURLCURLFESPACE
#define FILE_HCURLCURLFESPACE


/*********************************************************************/
/* File:   hcurlcurlfespace.h                                        */
/* Author: Michael Neunteufel                                        */
/* Date:   2018                                                      */
/*********************************************************************/


namespace ngcomp
{

  class HCurlCurlFESpace : public FESpace
  {
    size_t ndof;
    Array<int> first_facet_dof;
    Array<int> first_element_dof;
    Array<int> first_edge_dof;
    Array<INT<1,int> > order_edge;
    Array<INT<2,int> > order_facet;
    Array<INT<3,int> > order_inner;

    Array<bool> fine_facet;
    Array<bool> fine_edges;
    

    bool discontinuous;
    bool issurfacespace;
    int uniform_order_facet;
    int uniform_order_inner;
    int uniform_order_edge;

  public:
    HCurlCurlFESpace (shared_ptr<MeshAccess> ama, const Flags & flags, bool checkflags=false);

    virtual string GetClassName () const override
    {
      return "HCurlCurlFESpace";
    }
    static DocInfo GetDocu ();

    virtual void Update(LocalHeap & lh) override;

    virtual size_t GetNDof () const throw() override { return ndof; }

    virtual void SetOrder (NodeId ni, int order) override;
    virtual int GetOrder (NodeId ni) const override;
    
    virtual FiniteElement & GetFE (ElementId ei, Allocator & alloc) const override;
    

    virtual void GetVertexDofNrs (int vnr, Array<int> & dnums) const override
    {
      dnums.SetSize0();
    }
    virtual void GetEdgeDofNrs (int ednr, Array<int> & dnums) const override;

    virtual void GetFaceDofNrs (int fanr, Array<int> & dnums) const override;
    virtual void GetInnerDofNrs (int elnr, Array<int> & dnums) const override;

    void GetDofNrs (ElementId ei, Array<int> & dnums) const override;
    
    virtual void UpdateCouplingDofArray() override;

    virtual SymbolTable<shared_ptr<DifferentialOperator>> GetAdditionalEvaluators () const override;

  };

}

#endif
