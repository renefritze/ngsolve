#ifndef FILE_H1HOFE_IMPL
#define FILE_H1HOFE_IMPL

/*********************************************************************/
/* File:   h1hofe_impl.hpp                                           */
/* Author: Start                                                     */
/* Date:   6. Feb. 2003                                              */
/*********************************************************************/

#include "recursive_pol_tet.hpp"

namespace ngfem
{

  template <ELEMENT_TYPE ET> 
  class H1HighOrderFE_Shape : public H1HighOrderFE<ET, H1HighOrderFE_Shape<ET>>
  {
    using H1HighOrderFE<ET>::order;
    using H1HighOrderFE<ET>::order_edge;
    using H1HighOrderFE<ET>::order_face;
    using H1HighOrderFE<ET>::order_cell;

    using H1HighOrderFE<ET>::N_VERTEX;
    using H1HighOrderFE<ET>::N_EDGE;
    using H1HighOrderFE<ET>::N_FACE;

    static constexpr int DIM = ngfem::Dim(ET);

    // typedef LegendrePolynomial EdgeOrthoPol;
    typedef IntLegNoBubble EdgeOrthoPol;  // Integrated Legendre divided by bubble
    // typedef ChebyPolynomial EdgeOrthoPol; 
    typedef ChebyPolynomial QuadOrthoPol; 

  public:
    template<typename Tx, typename TFA>  
      INLINE void T_CalcShape (TIP<DIM,Tx> ip, TFA & shape) const;
    
    void CalcDualShape2 (const BaseMappedIntegrationPoint & mip, SliceVector<> shape) const
    { throw Exception ("dual shape not implemented, H1Ho"); }
    
  };



  /* *********************** Point  **********************/

  template<> template<typename Tx, typename TFA>  
  void H1HighOrderFE_Shape<ET_POINT> :: T_CalcShape (TIP<0,Tx> ip, TFA & shape) const
  {
    shape[0] = Tx(1.0);
  }



  /* *********************** Segment  **********************/  

  template <> template<typename Tx, typename TFA>  
  void H1HighOrderFE_Shape<ET_SEGM> :: T_CalcShape (TIP<1,Tx> ip, TFA & shape) const
  {
    Tx lam[2] = { ip.x, 1-ip.x };
    
    shape[0] = lam[0];
    shape[1] = lam[1];
    
    if (order_edge[0] >= 2)
      {
        INT<2> e = GetVertexOrientedEdge (0);
        EdgeOrthoPol::
          EvalMult (order_edge[0]-2, 
                    lam[e[1]]-lam[e[0]], lam[e[0]]*lam[e[1]], shape+2);
      }
  }


  /* *********************** Triangle  **********************/

  template<> template<typename Tx, typename TFA>  
  void H1HighOrderFE_Shape<ET_TRIG> :: T_CalcShape (TIP<2,Tx> ip, TFA & shape) const
  {
    Tx lam[3] = { ip.x, ip.y, 1-ip.x-ip.y };

    for (int i = 0; i < 3; i++) shape[i] = lam[i];

    int ii = 3;
    
    // edge-based shapes
    for (int i = 0; i < N_EDGE; i++)
      if (order_edge[i] >= 2)
	{ 
          INT<2> e = GetVertexOrientedEdge(i);
          EdgeOrthoPol::
            EvalScaledMult (order_edge[i]-2, 
                            lam[e[1]]-lam[e[0]], lam[e[0]]+lam[e[1]], 
                            lam[e[0]]*lam[e[1]], shape+ii);
	  ii += order_edge[i]-1;
	}

    // inner shapes
    if (order_face[0][0] >= 3)
      {
        // INT<4> f = GetFaceSort (0, vnums);
        INT<4> f = GetVertexOrientedFace (0);
	DubinerBasis::EvalMult (order_face[0][0]-3, 
                                lam[f[0]], lam[f[1]], 
                                lam[f[0]]*lam[f[1]]*lam[f[2]], shape+ii);
      }
  }


  template<>
  inline void H1HighOrderFE_Shape<ET_TRIG> ::CalcDualShape2 (const BaseMappedIntegrationPoint & mip, SliceVector<> shape) const
  {
    auto & ip = mip.IP();
    shape = 0.0;
    double lam[3] = { ip(0), ip(1), 1-ip(0)-ip(1) };
    size_t ii = 3;

    if (ip.VB() == BBND)
      {
	for (size_t i = 0; i < 3; i++)
	  shape[i] = (i == ip.FacetNr()) ? 1 : 0;
      }
    

    // edge-based shapes
    for (int i = 0; i < N_EDGE; i++)
      if (order_edge[i] >= 2)
	{
          if (ip.VB() == BND && ip.FacetNr() == i)
            {
              INT<2> e = GetVertexOrientedEdge(i);
              EdgeOrthoPol::
                EvalScaledMult (order_edge[i]-2, 
                                lam[e[1]]-lam[e[0]], lam[e[0]]+lam[e[1]], 
                                1.0/mip.GetMeasure() /* *lam[e[0]]*lam[e[1]]*/, shape+ii);
            }
          ii += order_edge[i]-1;
	}

    // inner shapes
    if (ip.VB() == VOL && order_face[0][0] >= 3)
      {
	INT<4> f = GetVertexOrientedFace (0);
	DubinerBasis::EvalMult(order_face[0][0]-3, 
                               lam[f[0]], lam[f[1]],1.0/mip.GetMeasure(), shape+ii);
      }
  }


  /* *********************** Quadrilateral  **********************/

  template<> template<typename Tx, typename TFA>  
  void H1HighOrderFE_Shape<ET_QUAD> :: T_CalcShape (TIP<2,Tx> ip, TFA & shape) const
  {
    Tx x = ip.x, y = ip.y;
    Tx hx[2] = { x, y };
    Tx lam[4] = {(1-x)*(1-y),x*(1-y),x*y,(1-x)*y};  
    
    // vertex shapes
    for(int i=0; i < N_VERTEX; i++) shape[i] = lam[i]; 
    int ii = 4;
     
    // edge-based shapes
    for (int i = 0; i < N_EDGE; i++)
      if (order_edge[i] >= 2)
        {
          int p = order_edge[i];

          Tx xi = ET_trait<ET_QUAD>::XiEdge(i, hx, this->vnums);
          Tx lam_e = ET_trait<ET_QUAD>::LamEdge(i, hx);
          
          Tx bub = 0.25 * lam_e * (1 - xi*xi);
          EdgeOrthoPol::EvalMult (p-2, xi, bub, shape+ii);
          ii += p-1;
        }
    
    // inner shapes
    INT<2> p = order_face[0];
    if (p[0] >= 2 && p[1] >= 2)
      {
        Vec<2,Tx> xi = ET_trait<ET_QUAD>::XiFace(0, hx, this->vnums);

	Tx bub = 1.0/16 * (1-xi(0)*xi(0))*(1-xi(1)*xi(1));
        
        /*
	ArrayMem<Tx,20> polxi(order+1), poleta(order+1);
        
	LegendrePolynomial::EvalMult(p[0]-2, xi(0), bub, polxi);
	LegendrePolynomial::Eval(p[1]-2, xi(1), poleta);
	
	for (int k = 0; k <= p[0]-2; k++)
	  for (int j = 0; j <= p[1]-2; j++)
	    shape[ii++] = polxi[k] * poleta[j];
        */

        QuadOrthoPol::EvalMult1Assign(p[0]-2, xi(0), bub,
          SBLambda ([&](int i, Tx val) LAMBDA_INLINE 
                    {  
                      QuadOrthoPol::EvalMult (p[1]-2, xi(1), val, shape+ii);
                      ii += p[1]-1;
                    }));
      }
  }


  /* *********************** Tetrahedron  **********************/

  template<> template<typename Tx, typename TFA>  
  INLINE void H1HighOrderFE_Shape<ET_TET> :: T_CalcShape (TIP<3,Tx> ip, TFA & shape) const
  {
    Tx lam[4] = { ip.x, ip.y, ip.z, 1-ip.x-ip.y-ip.z };

    // vertex shapes
    //for (int i = 0; i < 4; i++) shape[i] = lam[i];
    if (!nodalp2)
      for (int i = 0; i < 4; i++)
	shape[i] = lam[i];
    else
      for (int i = 0; i < 4; i++)
	shape[i] = 0.25*lam[i]*(2*lam[i]-1);

    int ii = 4; 

    // edge dofs
    if(!nodalp2) {
      for (int i = 0; i < N_EDGE; i++)
	if (order_edge[i] >= 2)
	  {
	    // INT<2> e = GetEdgeSort (i, vnums);
	    INT<2> e = GetVertexOrientedEdge (i);
	    EdgeOrthoPol::EvalScaledMult (order_edge[i]-2, 
					  lam[e[1]]-lam[e[0]], lam[e[0]]+lam[e[1]], 
					  lam[e[0]]*lam[e[1]], shape+ii);
	    ii += order_edge[i]-1;
	  }
    }
    else {
      for (int i = 0; i < N_EDGE; i++)
	if (order_edge[i] >= 2)
	  {
	    INT<2> e = GetEdgeSort (i, vnums);
	    
	    LegendrePolynomial::EvalScaledMult (order_edge[i]-2, 
						lam[e[1]]-lam[e[0]], lam[e[0]]+lam[e[1]], 
						lam[e[0]]*lam[e[1]], shape.Addr(ii));
	    ii += order_edge[i]-1;
	  }
    }
    
    // face dofs
    for (int i = 0; i < N_FACE; i++)
      if (order_face[i][0] >= 3)
	{
          // INT<4> f = GetFaceSort (i, vnums);
          INT<4> f = GetVertexOrientedFace (i);
	  int vop = 6 - f[0] - f[1] - f[2];  	
          
	  int p = order_face[i][0];
	  DubinerBasis::EvalScaledMult (p-3, lam[f[0]], lam[f[1]], 1-lam[vop], 
                                        lam[f[0]]*lam[f[1]]*lam[f[2]], shape+ii);
	  ii += (p-2)*(p-1)/2;
	}

    // interior shapes 
    if (order_cell[0][0] >= 4)

      DubinerBasis3D::EvalMult
	(order_cell[0][0]-4, lam[0], lam[1], lam[2],
	 lam[0]*lam[1]*lam[2]*lam[3], shape+ii);
	 
    // if (order_cell[0][0] >= 4)
    //   ii += TetShapesInnerLegendre::
    // 	Calc (order_cell[0][0], 
    // 	      lam[0]-lam[3], lam[1], lam[2], 
    // 	      shape+ii);
  }


  template<>
  inline void H1HighOrderFE_Shape<ET_TET> ::
  CalcDualShape2 (const BaseMappedIntegrationPoint & mip, SliceVector<> shape) const
  {
    auto & ip = mip.IP();
    shape = 0.0;
    double lam[4] = { ip(0), ip(1), ip(2), 1-ip(0)-ip(1)-ip(2) };
    size_t ii = 4;

    if (ip.VB() == BBBND)
      {
	for (size_t i = 0; i < 4; i++)
	  shape[i] = (i == ip.FacetNr()) ? 1 : 0;
      }
    // edge-based shapes
    for (int i = 0; i < N_EDGE; i++)
      {
	if (order_edge[i] >= 2 && ip.FacetNr() == i && ip.VB() == BBND)
	  {
	    INT<2> e = GetVertexOrientedEdge(i);
	    EdgeOrthoPol::
	      EvalScaledMult (order_edge[i]-2, 
			      lam[e[1]]-lam[e[0]], lam[e[0]]+lam[e[1]], 
			      1.0/mip.GetMeasure() /* *lam[e[0]]*lam[e[1]] */, shape+ii);
	  }
	
	ii += order_edge[i]-1;
      }
    // face shapes
    for (int i = 0; i < N_FACE; i++)
      {
	if (order_face[i][0] >= 3 && ip.FacetNr() == i && ip.VB() == BND)
	  {
	    INT<4> f = GetVertexOrientedFace (i);
	    DubinerBasis::EvalMult (order_face[i][0]-3, 
				     lam[f[0]], lam[f[1]], 1.0/mip.GetMeasure(), shape+ii);
	  }
	ii += (order_face[i][0]-2)*(order_face[i][0]-1)/2;
      }
    //inner shapes
    if (ip.VB() == VOL && order_cell[0][0] >= 4)
      DubinerBasis3D::EvalMult (order_cell[0][0]-4, lam[0], lam[1], lam[2], 1.0/mip.GetMeasure(), shape+ii);
  }


  /* *********************** Prism  **********************/

  template<> template<typename Tx, typename TFA>  
  void  H1HighOrderFE_Shape<ET_PRISM> :: T_CalcShape (TIP<3,Tx> ip, TFA & shape) const
  {
    Tx x = ip.x, y = ip.y, z = ip.z;
    Tx lam[6] = { x, y, 1-x-y, x, y, 1-x-y };
    Tx muz[6]  = { 1-z, 1-z, 1-z, z, z, z };

    Tx sigma[6];
    for (int i = 0; i < 6; i++) sigma[i] = lam[i] + muz[i];

    // vertex shapes
    for (int i = 0; i < 6; i++) shape[i] = lam[i] * muz[i];

    int ii = 6;

    // horizontal edge dofs
    for (int i = 0; i < 6; i++)
      if (order_edge[i] >= 2)
	{
          // INT<2> e = GetEdgeSort (i, vnums);
          INT<2> e = GetVertexOrientedEdge (i);

	  Tx xi = lam[e[1]]-lam[e[0]]; 
	  Tx eta = lam[e[0]]+lam[e[1]]; 
	  Tx bub = lam[e[0]]*lam[e[1]]*muz[e[1]];

	  EdgeOrthoPol::
	    EvalScaledMult (order_edge[i]-2, xi, eta, bub, shape+ii);
	  ii += order_edge[i]-1;
	}
    
    // vertical edges
    for (int i = 6; i < 9; i++)
      if (order_edge[i] >= 2)
	{
          // INT<2> e = GetEdgeSort (i, vnums);
          INT<2> e = GetVertexOrientedEdge (i);

	  EdgeOrthoPol::
	    EvalMult (order_edge[i]-2, 
		      muz[e[1]]-muz[e[0]], 
		      muz[e[0]]*muz[e[1]]*lam[e[1]], shape+ii);

	  ii += order_edge[i]-1;
	}
    

    ArrayMem<Tx,20> polx(order+1), poly(order+1), polz(order+1);

    // trig face dofs
    for (int i = 0; i < 2; i++)
      if (order_face[i][0] >= 3)
	{
          // INT<4> f = GetFaceSort (i, vnums);
          INT<4> f = GetVertexOrientedFace (i);
	  int p = order_face[i][0];
	  
	  Tx bub = lam[0]*lam[1]*lam[2]*muz[f[2]];
	  
	  DubinerBasis::
	    EvalMult (p-3, lam[f[0]], lam[f[1]], bub, shape+ii);

	  ii += (p-2)*(p-1)/2; 
	}
   
    // quad face dofs
    for (int i = 2; i < 5; i++)
      if (order_face[i][0] >= 2 && order_face[i][1] >= 2)
	{
	  INT<2> p = order_face[i];
          // INT<4> f = GetFaceSort (i, vnums);
          INT<4> f = GetVertexOrientedFace (i);          

	  Tx xi  = sigma[f[0]] - sigma[f[1]]; 
	  Tx eta = sigma[f[0]] - sigma[f[3]];

	  Tx scalexi(1.0), scaleeta(1.0);
	  if (f[0] / 3 == f[1] / 3)  
	    scalexi = lam[f[0]]+lam[f[1]];  // xi is horizontal
	  else
	    scaleeta = lam[f[0]]+lam[f[3]];

	  Tx bub = (1.0/16)*(scaleeta*scaleeta-eta*eta)*(scalexi*scalexi-xi*xi);
	  QuadOrthoPol::EvalScaled     (p[0]-2, xi, scalexi, polx);
	  QuadOrthoPol::EvalScaledMult (p[1]-2, eta, scaleeta, bub, poly);
	    
	  for (int k = 0; k < p[0]-1; k++) 
            for (int j = 0; j < p[1]-1; j++) 
              shape[ii++] = polx[k] * poly[j];
	}
    
    // volume dofs:
    INT<3> p = order_cell[0];
    if (p[0] > 2 && p[2] > 1)
      {
	int nf = (p[0]-1)*(p[0]-2)/2;
	ArrayMem<Tx,20> pol_trig(nf);

	DubinerBasis::EvalMult (p[0]-3, x, y, x*y*(1-x-y),pol_trig);
	LegendrePolynomial::EvalMult (p[2]-2, 2*z-1, z*(1-z), polz);

	for (int i = 0; i < nf; i++)
	  for (int k = 0; k < p[2]-1; k++)
	    shape[ii++] = pol_trig[i] * polz[k];
      }
  }



 

  /* *********************** Hex  **********************/

  template<> template<typename Tx, typename TFA>  
  void  H1HighOrderFE_Shape<ET_HEX> :: T_CalcShape (TIP<3,Tx> ip, TFA & shape) const
  { 
    Tx x = ip.x, y = ip.y, z = ip.z;

    Tx lam[8]={(1-x)*(1-y)*(1-z),x*(1-y)*(1-z),x*y*(1-z),(1-x)*y*(1-z),
		(1-x)*(1-y)*z,x*(1-y)*z,x*y*z,(1-x)*y*z}; 
    Tx sigma[8]={(1-x)+(1-y)+(1-z),x+(1-y)+(1-z),x+y+(1-z),(1-x)+y+(1-z),
		 (1-x)+(1-y)+z,x+(1-y)+z,x+y+z,(1-x)+y+z}; 

    // vertex shapes
    for (int i = 0; i < 8; i++) shape[i] = lam[i]; 
    int ii = 8;

    ArrayMem<Tx,30> polx(order+1), poly(order+1), polz(order+1);
    
    // edge dofs
    for (int i = 0; i < N_EDGE; i++)
      if (order_edge[i] >= 2)
	{
	  int p = order_edge[i];

          // INT<2> e = GetEdgeSort (i, vnums);
          INT<2> e = GetVertexOrientedEdge (i);          
          Tx xi = sigma[e[1]]-sigma[e[0]]; 
          Tx lam_e = lam[e[0]]+lam[e[1]];
	  Tx bub = 0.25 * lam_e * (1 - xi*xi);
	  
	  EdgeOrthoPol::EvalMult (p-2, xi, bub, shape+ii);
	  ii += p-1;
	}

    for (int i = 0; i < N_FACE; i++)
      if (order_face[i][0] >= 2 && order_face[i][1] >= 2)
	{
	  INT<2> p = order_face[i];
          // INT<4> f = GetFaceSort (i, vnums);	  
          INT<4> f = GetVertexOrientedFace (i);
	  Tx lam_f(0.0);
	  for (int j = 0; j < 4; j++) lam_f += lam[f[j]];
          
	  Tx xi  = sigma[f[0]] - sigma[f[1]]; 
	  Tx eta = sigma[f[0]] - sigma[f[3]];

	  Tx bub = 1.0/16 * (1-xi*xi)*(1-eta*eta) * lam_f;
	  QuadOrthoPol::EvalMult(p[0]-2, xi, bub, polx);
	  QuadOrthoPol::Eval(p[1]-2, eta, poly);

	  for (int k = 0; k < p[0]-1; k++) 
            for (int j = 0; j < p[1]-1; j++) 
              shape[ii++]= polx[k] * poly[j];
	}

    // volume dofs:
    INT<3> p = order_cell[0];
    if (p[0] >= 2 && p[1] >= 2 && p[2] >= 2)
      {
	QuadOrthoPol::EvalMult (p[0]-2, 2*x-1, x*(1-x), polx);
	QuadOrthoPol::EvalMult (p[1]-2, 2*y-1, y*(1-y), poly);
	QuadOrthoPol::EvalMult (p[2]-2, 2*z-1, z*(1-z), polz);

	for (int i = 0; i < p[0]-1; i++)
	  for (int j = 0; j < p[1]-1; j++)
	    {
	      Tx pxy = polx[i] * poly[j];
	      for (int k = 0; k < p[2]-1; k++)
		shape[ii++] = pxy * polz[k];
	    }
      }
  }

  /* ******************************** Pyramid  ************************************ */

  template<> template<typename Tx, typename TFA>  
  void  H1HighOrderFE_Shape<ET_PYRAMID> :: T_CalcShape (TIP<3,Tx> ip, TFA & shape) const
  {
    Tx x = ip.x, y = ip.y, z = ip.z;

    // if (z == 1.) z -= 1e-10;
    z *= (1-1e-10);

    Tx xt = x / (1-z);
    Tx yt = y / (1-z);
    
    Tx sigma[4]  = { (1-xt)+(1-yt), xt+(1-yt), xt+yt, (1-xt)+yt };
    Tx lambda[4] = { (1-xt)*(1-yt), xt*(1-yt), xt*yt, (1-xt)*yt };
    Tx lambda3d[5];

    for (int i = 0; i < 4; i++)  
      lambda3d[i] = lambda[i] * (1-z);
    lambda3d[4] = z;


    for (int i = 0; i < 5; i++)  
      shape[i] = lambda3d[i];

    int ii = 5;


    // horizontal edge dofs 
    for (int i = 0; i < 4; i++)
      if (order_edge[i] >= 2)
	{
	  int p = order_edge[i];
	  // INT<2> e = GetEdgeSort (i, vnums);	  
          INT<2> e = GetVertexOrientedEdge (i);
          
	  Tx xi = sigma[e[1]]-sigma[e[0]]; 
	  Tx lam_e = lambda[e[0]]+lambda[e[1]];
	  Tx bub = 0.25 * lam_e * (1 - xi*xi)*(1-z)*(1-z);
	  Tx ximz = xi*(1-z);
	  EdgeOrthoPol::
	    EvalScaledMult (p-2, ximz, 1-z, bub, shape+ii);
	  ii += p-1;
	}
    
    // vertical edges
    for (int i = 4; i < 8; i++) 
      if (order_edge[i] >= 2)
	{
	  int p = order_edge[i];
	  // INT<2> e = GetEdgeSort (i, vnums);	  
          INT<2> e = GetVertexOrientedEdge (i);
          
	  Tx xi = lambda3d[e[1]]-lambda3d[e[0]]; 
	  Tx lam_e = lambda3d[e[0]]+lambda3d[e[1]];
	  Tx bub = 0.25 * (lam_e*lam_e-xi*xi);
	  
	  EdgeOrthoPol::
	    EvalScaledMult (p-2, xi, lam_e, bub, shape+ii);
	  ii += p-1;
	}


    ArrayMem<Tx,20> polx(order+1), poly(order+1), polz(order+1);
    const FACE * faces = ElementTopology::GetFaces (ET_PYRAMID);

    // trig face dofs
    for (int i = 0; i < 4; i++)
      if (order_face[i][0] >= 3)
	{
	  int p = order_face[i][0];
	  Tx lam_face = lambda[faces[i][0]] + lambda[faces[i][1]];  // vertices on quad    

	  Tx bary[5] = 
	    {(sigma[0]-lam_face)*(1-z), (sigma[1]-lam_face)*(1-z), 
	     (sigma[2]-lam_face)*(1-z), (sigma[3]-lam_face)*(1-z), z };
	  
	  // INT<4> f = GetFaceSort (i, vnums);
          INT<4> f = GetVertexOrientedFace (i);
          
	  Tx bub = lam_face * bary[f[0]]*bary[f[1]]*bary[f[2]];

	  DubinerBasis::
	    EvalMult (p-3, bary[f[0]], bary[f[1]], bub, shape+ii);
	  ii += (p-2)*(p-1)/2;
	}
    
    // quad face dof
    if (order_face[4][0] >= 2 && order_face[4][1] >= 2)
      {	  
	INT<2> p = order_face[4];

	int pmax = max2(p[0], p[1]);
	Tx fac(1.0);
	for (int k = 1; k <= pmax; k++)
	  fac *= (1-z);

	// INT<4> f = GetFaceSort (4, vnums);	  
        INT<4> f = GetVertexOrientedFace (4);
        
	Tx xi  = sigma[f[0]] - sigma[f[1]]; 
	Tx eta = sigma[f[0]] - sigma[f[3]];

	QuadOrthoPol::EvalMult (p[0]-2, xi,  0.25*(1-xi*xi), polx);
	QuadOrthoPol::EvalMult (p[1]-2, eta, 0.25*(1-eta*eta), poly);
	for (int k = 0; k < p[0]-1; k++) 
	  for (int j = 0; j < p[1]-1; j++) 
	    shape[ii++] = polx[k] * poly[j] * fac; 
      }

    
    if (order_cell[0][0] >= 3)
      {
	LegendrePolynomial::EvalMult (order_cell[0][0]-2, 2*xt-1, xt*(1-xt), polx);
        LegendrePolynomial::EvalMult (order_cell[0][0]-2, 2*yt-1, yt*(1-yt), poly);

	Tx pz = z*(1-z)*(1-z);
	
	for(int k = 0; k <= order_cell[0][0]-3; k++)
	  {
	    for(int i = 0; i <= k; i++)
	      { 
		Tx bubpik = pz * polx[i];
		for (int j = 0; j <= k; j++)
		  shape[ii++] = bubpik * poly[j];
	      }
	    pz *= 1-z;  
	  }
      }
  }

}

#endif
