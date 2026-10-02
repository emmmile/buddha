# Uniform against Newton seeding at four windows (research doc, "1. Newton seeding", first table).
# diag.py and diag2.py import evaluate() from here.
import numpy as np, time
LOW,HIGH=32,8192
def evaluate(c, w, half):
    # z0=c as in the shader; returns L (escape iter or -1), C (visits in window)
    z=c.copy(); L=np.full(c.shape,-1); C=np.zeros(c.shape,int); alive=np.ones(c.shape,bool)
    for i in range(HIGH):
        inwin=(abs(z.real-w.real)<half)&(abs(z.imag-w.imag)<half)&alive
        C+=inwin
        esc=alive&(abs(z)**2>8); L[esc]=i; alive&=~esc
        z=np.where(alive,z*z+c,z)
        if not alive.any(): break
    return L,C
def good(c,w,half):
    L,C=evaluate(c,w,half); return (L>LOW)&(C>0), L, C
if __name__ == "__main__":
    rng=np.random.default_rng(1)
    for w,half in [(-0.1+0.75j,1e-2),(-0.1+0.75j,1e-4),(0.3+0.2j,1e-4),(-1.2+0.15j,1e-5)]:
        N=200000
        c=rng.uniform(-2,2,N)+1j*rng.uniform(-2,2,N)
        ok,_,_=good(c,w,half)
        pu=ok.mean()
        # Newton: solve z_n(c)=target, z_0=c (z_k = f^k with z0=c means z_n = p_{n+1}(c))
        M=4000
        n=rng.integers(2,40,M)
        tgt=w+(rng.uniform(-half,half,M)+1j*rng.uniform(-half,half,M))
        c=rng.uniform(-2,0.5,M)+1j*rng.uniform(-1.2,1.2,M)
        steps=0
        for it in range(30):
            z=c.copy(); d=np.ones(M,complex)
            for k in range(n.max()):
                m=k<n
                d=np.where(m,2*z*d+1,d); z=np.where(m,z*z+c,z)
                steps+=m.sum()
            with np.errstate(all='ignore'):
                step=(z-tgt)/d
            step=np.where(np.isfinite(step),step,0)
            c=c-np.clip(abs(step),0,0.5)*np.exp(1j*np.angle(step))
        ok,L,C=good(c,w,half)
        print(f"w={w} half={half:g}: uniform hit rate {pu:.2e} ; newton hit rate {ok.mean():.3f}, median L {np.median(L[ok]) if ok.any() else 0:.0f}, median C {np.median(C[ok]) if ok.any() else 0:.0f}, newton orbit-steps/seed {steps/M:.0f}")
