from netgen.geom2d import *
from netgen.csg import unit_cube
from ngsolve import *
import pickle
import numpy
import io

def test_pickle_volume_fespaces():
    mesh = Mesh(unit_square.GenerateMesh(maxh=0.3))
    spaces = [H1(mesh,order=3,dirichlet=[1,2,3,4]), VectorH1(mesh,order=3,dirichlet=[1,2,3,4]), L2(mesh,order=3), VectorL2(mesh,order=3), SurfaceL2(mesh,order=3), HDivDiv(mesh,order=3,dirichlet=[1,2,3,4]), VectorFacet(mesh,order=3,dirichlet=[1,2,3,4]), FacetFESpace(mesh,order=3,dirichlet=[1,2,3,4]), NumberSpace(mesh), HDiv(mesh,order=3,dirichlet=[1,2,3,4]), HCurl(mesh,order=3,dirichlet=[1,2,3,4])]
    
    for space in spaces:
        data = pickle.dumps(space)
        space2 = pickle.loads(data)
        assert space.ndof == space2.ndof
        
def test_pickle_surface_fespaces():
    import netgen.meshing as meshing
    import netgen.csg as csg 
    geo = csg.CSGeometry()
    bottom   = csg.Plane (csg.Pnt(0,0,0), csg.Vec(0,0, 1) )
    surface = csg.SplineSurface(bottom)
    pts = [(0,0,0),(0,1,0),(1,1,0),(1,0,0)]
    geopts = [surface.AddPoint(*p) for p in pts]
    for p1,p2,bc in [(0,1,"left"), (1, 2,"top"),(2,3,"right"),(3,0,"bottom")]:
        surface.AddSegment(geopts[p1],geopts[p2],bc)
    geo.AddSplineSurface(surface)
    mesh = Mesh(geo.GenerateMesh(maxh=0.3, perfstepsend=meshing.MeshingStep.MESHSURFACE))

    spaces = [HDivDivSurface(mesh,order=3,dirichlet=[1,2,3,4]), FacetSurface(mesh,order=3,dirichlet=[1,2,3,4]), HDivSurface(mesh,order=3,dirichlet=[1,2,3,4])]

    for space in spaces:
        data = pickle.dumps(space)
        space2 = pickle.loads(data)
        assert space.ndof == space2.ndof
        
def test_pickle_gridfunction_real():
    mesh = Mesh(unit_square.GenerateMesh(maxh=0.3))
    fes = H1(mesh,order=3,dirichlet=[1,2,3,4])
    u = GridFunction(fes,"u")
    u.Set(x*y)

    data = pickle.dumps((u, grad(u)))
    u2, gradu2 = pickle.loads(data)
    assert sqrt(Integrate((u-u2)*(u-u2),mesh)) < 1e-14
    assert sqrt(Integrate(InnerProduct(grad(u)-gradu2, grad(u)-gradu2), mesh)) < 1e-14

def test_pickle_multidim():
    mesh = Mesh(unit_cube.GenerateMesh(maxh=0.4))
    fes = H1(mesh,order=3,dim=3)
    u = GridFunction(fes)
    u.Set((1,2,3))
    pickled = pickle.dumps((u,grad(u)))
    u2, gradu2 = pickle.loads(pickled)
    assert numpy.linalg.norm(u.vec.FV().NumPy() - u2.vec.FV().NumPy()) < 1e-14
    assert Integrate(Norm(grad(u)-gradu2), mesh) < 1e-14

def test_pickle_secondorder_mesh():
    m = unit_cube.GenerateMesh(maxh=0.4)
    m.SecondOrder()
    mesh = Mesh(m)
    fes = H1(mesh, order=3)
    u = GridFunction(fes)
    u.Set(x)
    pickled = pickle.dumps(u)
    u2 = pickle.loads(pickled)
    assert numpy.linalg.norm(u.vec.FV().NumPy() - u2.vec.FV().NumPy()) < 1e-14


def test_pickle_gridfunction_complex():
    mesh = Mesh(unit_square.GenerateMesh(maxh=0.3))
    fes = HCurl(mesh,order=3,complex=True)
    u = GridFunction(fes,"u")
    u.Set((x*y,1J * y))
    data = pickle.dumps((u, curl(u)))
    u2, curlu2  = pickle.loads(data)
    difvec = u.vec.CreateVector()
    difvec.data = u.vec - u2.vec
    assert Norm(difvec) < 1e-12
    assert Integrate(Norm(curl(u)-curlu2), mesh) < 1e-14

def test_pickle_hcurl():
    mesh = Mesh(unit_square.GenerateMesh(maxh=0.3))
    fes = HCurl(mesh,order=3)
    u = GridFunction(fes)
    u.Set(CoefficientFunction((1,1)))
    with io.BytesIO() as f:
        pickler = pickle.Pickler(f)
        pickler.dump(u)
        data = f.getvalue()

    with io.BytesIO(data) as f:
        unpickler = pickle.Unpickler(f)
        u2 = unpickler.load()
    error = sqrt(Integrate(Conj(u-u2)*(u-u2),mesh))
    assert error.real < 1e-14 and error.imag < 1e-14

def test_pickle_compoundfespace():
    mesh = Mesh(unit_square.GenerateMesh(maxh=0.3))
    fes1 = HDiv(mesh,order=2)
    fes2 = L2(mesh,order=1)
    fes = FESpace([fes1,fes2])
    sigma, u = fes.TrialFunction()
    tau,v = fes.TestFunction()
    a = BilinearForm(fes)
    a += SymbolicBFI(sigma * tau + div(sigma)*v + div(tau)*u - 1e-10*u*v)

    f = LinearForm(fes)
    f += SymbolicLFI(-v)

    u = GridFunction(fes,"u")
    with TaskManager():
        a.Assemble()
        f.Assemble()
        u.vec.data = a.mat.Inverse(fes.FreeDofs()) * f.vec

    with io.BytesIO() as f:
        pickler = pickle.Pickler(f)
        pickler.dump(u)
        data = f.getvalue()

    with io.BytesIO(data) as f:
        unpickler = pickle.Unpickler(f)
        u2 = unpickler.load()
    flux1, flux2 = u.components[0], u2.components[0]
    sol1, sol2 = u.components[1], u2.components[1]
    error = sqrt(Integrate((sol1-sol2)*(sol1-sol2),mesh))
    errorflux = sqrt(Integrate((flux1[0]-flux2[0])*(flux1[0]-flux2[0])+(flux1[1]-flux2[1])*(flux1[1]-flux2[1]),mesh))
    assert error < 1e-14 and errorflux < 1e-14

def test_pickle_periodic():
    periodic = SplineGeometry()
    pnts = [ (0,0), (1,0), (1,1), (0,1) ]
    pnums = [periodic.AppendPoint(*p) for p in pnts]
    periodic.Append ( ["line", pnums[0], pnums[1]],bc="outer")
    lright = periodic.Append ( ["line", pnums[1], pnums[2]], bc="periodic")
    periodic.Append ( ["line", pnums[2], pnums[3]], bc="outer")
    periodic.Append ( ["line", pnums[0], pnums[3]], leftdomain=0, rightdomain=1, copy=lright, bc="periodic")
    mesh = Mesh(periodic.GenerateMesh(maxh=0.2))
    fes = Periodic(H1(mesh,order=3,dirichlet="outer"))
    u,v = fes.TrialFunction(), fes.TestFunction()
    a = BilinearForm(fes,symmetric=True)
    a += SymbolicBFI(grad(u) * grad(v))
    f = LinearForm(fes)
    f += SymbolicLFI(x*v)
    u = GridFunction(fes,"u")
    with TaskManager():
        a.Assemble()
        f.Assemble()
        u.vec.data = a.mat.Inverse(fes.FreeDofs()) * f.vec
    data = pickle.dumps(u)
    u2 = pickle.loads(data)
    assert sqrt(Integrate((u-u2)*(u-u2),mesh)) < 1e-14

def test_pickle_CoefficientFunctions():
    import netgen.csg as csg
    import numpy as np
    import numpy.testing as npt
    mesh = Mesh(csg.unit_cube.GenerateMesh(maxh=1000))
    cfs = []
    cfs.append(CoefficientFunction(1)) # ConstantCF
    cfs.append(CoefficientFunction(1J)) # ConstantCFC
    cfs.append(Parameter(3)) # ParameterCF
    cfs.append(CoefficientFunction([cfs[2]])) # Domainwise with parameter inside
    # coordinate
    cfs.append(x)
    cfs.append(y)
    cfs.append(z)
    # unary
    cfs.append(sin(x))
    cfs.append(cos(y))
    cfs.append(floor(z))
    # binary
    cfs.append(x**y)
    cfs.append(y*z)
    cfs.append(x/y)
    cfs.append(x+cfs[2])
    cfs.append(cfs[2]-y)
    vec = CoefficientFunction((cfs[2],1,2))
    mat = CoefficientFunction((5,cfs[2],0,2),dims=(2,2))
    cfs.append(vec) # Vectorial
    cfs.append(vec[0]) # Component
    cfs.append(2 * vec) # scale
    cfs.append(vec * vec) # MultVecVec
    cfs.append(Norm(vec)) # Norm
    cfs.append(mat) # Mat
    cfs.append(mat.trans)
    cfs.append(mat.Eig())
    cfs.append(IfPos(x-cfs[2], y, z)) # Ifpos
    imagcf = x * 1J + y
    cfs.append(imagcf)
    cfs.append(imagcf.real)
    cfs.append(imagcf.imag)
    cfs.append(Norm(imagcf))
    to_compile = Inv(Cof(mat))
    cfs.append(to_compile.Compile())
    cfs.append(to_compile.Compile(realcompile=True, wait=True))
    cfs.append(to_compile.Compile(realcompile=True, wait=False))
    mp = mesh(0.3,0.4,0.5)
    compiled_vals = to_compile(mp)
    dump = pickle.dumps(cfs)
    lcfs = pickle.loads(dump)
    assert lcfs[0](mp) == 1
    assert lcfs[1](mp) == 1J
    assert lcfs[2].Get() == 3
    assert lcfs[3](mp) == 3, lcfs[3](mp)
    lcfs[2].Set(5)
    assert lcfs[2](mp) == 5
    assert lcfs[3](mp) == 5
    npt.assert_approx_equal(lcfs[4](mp),0.3)
    npt.assert_approx_equal(lcfs[5](mp),0.4)
    npt.assert_approx_equal(lcfs[6](mp),0.5)
    npt.assert_approx_equal(lcfs[7](mp),sin(0.3))
    npt.assert_approx_equal(lcfs[8](mp),cos(0.4))
    assert lcfs[9](mp) == 0
    npt.assert_approx_equal(lcfs[10](mp), 0.3**0.4)
    npt.assert_approx_equal(lcfs[11](mp), 0.4*0.5)
    npt.assert_approx_equal(lcfs[12](mp), 0.3/0.4)
    npt.assert_approx_equal(lcfs[13](mp), 5.3)
    npt.assert_approx_equal(lcfs[14](mp), 4.6)
    assert lcfs[15](mp) == (5,1,2)
    lcfs[2].Set(8)
    assert lcfs[16](mp) == 8
    assert lcfs[17](mp) == (16,2,4)
    assert lcfs[18](mp) == 69
    assert lcfs[19](mp) == sqrt(69)
    assert lcfs[20](mp) == (5,8,0,2), lcfs[20](mp)
    assert lcfs[21](mp) == (5,0,8,2)
    lcfs[2].Set(0)
    assert lcfs[22](mp) == (1,0,0,1,5,2)
    lcfs[2].Set(0.5)
    npt.assert_approx_equal(lcfs[23](mp),0.5)
    npt.assert_approx_equal(lcfs[23](mesh(0.7,0.1,0.5)),0.1)
    npt.assert_approx_equal(lcfs[24](mp).real,0.4)
    npt.assert_approx_equal(lcfs[24](mp).imag,0.3)
    npt.assert_approx_equal(lcfs[25](mp),0.4)
    npt.assert_approx_equal(lcfs[26](mp),0.3)
    npt.assert_approx_equal(lcfs[27](mp),sqrt(0.3*0.3+0.4*0.4))
    np.allclose(lcfs[28](mp), compiled_vals)
    np.allclose(lcfs[29](mp), compiled_vals)
    np.allclose(lcfs[30](mp), compiled_vals)

if __name__ == "__main__":
    test_pickle_volume_fespaces()
    test_pickle_surface_fespaces()
    test_pickle_gridfunction_real()
    test_pickle_gridfunction_complex()
    test_pickle_compoundfespace()
    test_pickle_hcurl()
    test_pickle_periodic()
    test_pickle_CoefficientFunctions()
    test_pickle_multidim()
    test_pickle_secondorder_mesh()
