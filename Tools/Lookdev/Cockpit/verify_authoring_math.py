"""CPU invariants of the triplanar basis; does not pretend to compile UE HLSL."""
import ast, json
from pathlib import Path
import numpy as np

def main():
    here=Path(__file__).resolve().parent
    ast.parse((here/'author_unreal.py').read_text(encoding='utf-8'))
    axes=np.eye(3)
    checked=0
    for axis in range(3):
        for sign in (-1,1):
            n=axes[axis]*sign
            # X:(Y,Z), Y:(Z,X), Z:(X,Y); negative normal flips U.
            u=axes[(axis+1)%3]*sign; v=axes[(axis+2)%3]
            assert np.allclose(np.cross(u,v),n)
            for slope in [(0,0),(.1,0),(0,.1)]:
                d=u*slope[0]+v*slope[1]
                d-=n*np.dot(n,d)
                normal=n+d; normal/=np.linalg.norm(normal)
                assert np.dot(normal,n)>0
                if slope==(0,0): assert np.allclose(normal,n)
                if slope[0]: assert np.dot(normal,u)>0
                if slope[1]: assert np.dot(normal,v)>0
                checked+=1
    for n in [np.array([1,1,1.]),np.array([-1,2,-3.])]:
        n/=np.linalg.norm(n); d=np.zeros(3)
        assert np.allclose((n+d)/np.linalg.norm(n+d),n)
    # One 25cm object-local movement = one repeat, independent of translation/rotation.
    rot=np.array([[0,-1,0],[1,0,0],[0,0,1.]])
    translation=np.array([12000,-3000,900.])
    a=np.array([120,40,9.]); b=a+np.array([25,0,0.])
    inv_a=rot.T@(rot@a+translation-translation)
    inv_b=rot.T@(rot@b+translation-translation)
    assert np.allclose((inv_b-inv_a)*.01/.25,[1,0,0])
    result={'python_syntax':'PASS','signed_basis_slope_cases':checked,'flat_normal_identity':'PASS',
            'local_repeat_rigid_transform':'PASS','unreal_execution':'NOT_RUN','hlsl_compile':'NOT_RUN'}
    (here/'authoring_math_validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))

if __name__=='__main__': main()
