"""ASTER-24 V2: new low wedge, integrated canopy, sculpted shoulders and ring engines.

Executed by build_explorer.py with the common Blender construction functions.
The supplied generated design is a visual reference, not measured engineering data.
"""
from mathutils import Matrix
DETAIL = not opts.draft
COPPER = material("M_HeatCopper", (.31,.17,.068), .94, .34)
HEAT_BLUE = material("M_HeatBlue", (.09,.17,.22), .94, .38)
ETCH = material("M_EtchedTitanium", (.065,.085,.095), .7, .55)
bolt_batches={}


def bolt(loc, normal=(0,0,1), radius=.023):
    """A true recessed hex fastener, batched by part instead of thousands of objects."""
    if not DETAIL: return
    batch=bolt_batches.setdefault(group,{"verts":[],"faces":[],"materials":[]})
    v=batch["verts"]; f=batch["faces"]; mats=batch["materials"]
    start=len(v); pos=Vector(loc); q=Vector(normal).to_track_quat("Z","Y")
    profiles=[(.88,0),(1,.003),(1,.016),(.39,.016),(.39,.010)]
    for r,z in profiles:
        for i in range(6):
            ang=2*math.pi*i/6
            v.append(pos+q@Vector((radius*r*math.cos(ang),radius*r*math.sin(ang),z)))
    for j in range(4):
        for i in range(6):
            f.append(tuple(start+n for n in (j*6+i,j*6+(i+1)%6,(j+1)*6+(i+1)%6,(j+1)*6+i)))
            mats.append(1 if j==3 else 0)
    f.append(tuple(start+i for i in range(5,-1,-1)));mats.append(0)
    f.append(tuple(start+24+i for i in range(6)));mats.append(1)


def flush_fasteners():
    global group
    keep=group
    for name,b in bolt_batches.items():
        group=name
        o=mesh("Precision recessed fasteners",b["verts"],b["faces"],TITAN)
        o.data.materials.append(BLACK)
        for p,idx in zip(o.data.polygons,b["materials"]):p.material_index=idx
    group=keep


def inset_poly(points, margin=.005):
    """Planar edge offset; preserves mm seams on triangles and large armor plates."""
    p=[Vector(v) for v in points]
    u=(p[1]-p[0]).normalized();n=(p[1]-p[0]).cross(p[2]-p[0]).normalized();v=n.cross(u)
    xy=[Vector(((a-p[0]).dot(u),(a-p[0]).dot(v))) for a in p]
    def cross(a,b):return a.x*b.y-a.y*b.x
    sign=1 if sum(cross(xy[i],xy[(i+1)%len(xy)]) for i in range(len(xy)))>0 else -1
    q=[]
    for i,a in enumerate(xy):
        d1=(a-xy[i-1]).normalized();d2=(xy[(i+1)%len(xy)]-a).normalized()
        a1=a+Vector((-d1.y,d1.x))*margin*sign
        a2=a+Vector((-d2.y,d2.x))*margin*sign
        den=cross(d1,d2)
        z=a1+d1*(cross(a2-a1,d2)/den) if abs(den)>.00001 else (a1+a2)/2
        q.append(p[i]+u*(z.x-a.x)+v*(z.y-a.y))
    return q


def armor(name, points, mat=CERAMIC, lift=.045, thickness=.07, fasteners=True, force_up=True):
    p=inset_poly(points,.004)
    n=(p[1]-p[0]).cross(p[2]-p[0]).normalized()
    if force_up and n.z < 0:
        n=-n
        p.reverse()
    p=[v+n*lift for v in p]
    o=plate(name,p,mat,thickness,.005)
    # A complete panel UV island gives edge wear a physical relationship to seams.
    u=(p[1]-p[0]).normalized();v=n.cross(u).normalized()
    uu=[q.dot(u) for q in p];vv=[q.dot(v) for q in p]
    ulo,uhi=min(uu),max(uu);vlo,vhi=min(vv),max(vv)
    uv=o.data.uv_layers.active.data
    for face in o.data.polygons:
        if abs(face.normal.dot(n))<.65:continue
        for li in face.loop_indices:
            q=o.data.vertices[o.data.loops[li].vertex_index].co
            uv[li].uv=((q.dot(u)-ulo)/max(.001,uhi-ulo),(q.dot(v)-vlo)/max(.001,vhi-vlo))
    if fasteners:
        inner=inset_poly(p,.055)
        for q in inner:bolt(q+n*.004,n,.020)
        for i,a in enumerate(inner):
            b=inner[(i+1)%len(inner)];count=int((b-a).length/.55)
            for j in range(1,count):bolt(a.lerp(b,j/count)+n*.004,n,.017)
    return o


def annulus(name,x0,x1,y,z,r0,r1,inner0,inner1,mat,segments=96):
    verts=[]
    for x,r in ((x0,r0),(x1,r1),(x1,inner1),(x0,inner0)):
        for i in range(segments):
            a=2*math.pi*i/segments
            verts.append((x,y+r*math.cos(a),z+r*math.sin(a)))
    faces=[]
    for j in range(4):
        for i in range(segments):
            faces.append((j*segments+i,j*segments+(i+1)%segments,((j+1)%4)*segments+(i+1)%segments,((j+1)%4)*segments+i))
    return mesh(name,verts,faces,mat,smooth=True)


def curved_band(name,x0,x1,y,z,r,a0,a1,mat,thick=.045,steps=10):
    vv=[]
    for x,rr in ((x0,r),(x1,r),(x1,r-thick),(x0,r-thick)):
        for i in range(steps+1):
            a=a0+(a1-a0)*i/steps
            vv.append((x,y+rr*math.cos(a),z+rr*math.sin(a)))
    m=steps+1;ff=[]
    for j in range(4):
        for i in range(steps):ff.append((j*m+i,j*m+i+1,((j+1)%4)*m+i+1,((j+1)%4)*m+i))
    ff.extend([(0,m,2*m,3*m),(steps,3*m+steps,2*m+steps,m+steps)])
    return mesh(name,vv,ff,mat,.006,smooth=True)


# The cross section explicitly drops its center floor under the canopy, while the
# outer shoulders stay high. There is no opaque roof skin over the forward eye.
# x, half width, center z, shoulder z, outer z, keel z
RINGS=[(-8.50,5.90,1.20,1.10,.65,-.90),(-6.55,6.35,1.65,1.48,1.05,-1.00),
       (-3.20,6.30,1.88,1.63,1.06,-1.08),(.55,5.82,1.93,1.65,.95,-1.08),
       (3.20,5.15,1.88,1.55,.77,-.95),(4.60,4.52,.02,1.43,.66,-.80),
       (7.30,3.45,-.08,1.00,.36,-.67),(10.00,2.12,-.24,.37,-.11,-.55),
       (12.00,1.30,-.32,-.22,-.36,-.63)]


def section2(r):
    x,w,c,h,e,b=r;cy=min(1.65,w*.48)
    return [Vector((x,-cy,c)),Vector((x,cy,c)),Vector((x,w*.58,h)),
            Vector((x,w*.90,e)),Vector((x,w,e-.18)),Vector((x,w*.955,max(b+.08,e-.57))),
            Vector((x,w*.70,b)),Vector((x,-w*.70,b)),Vector((x,-w*.955,max(b+.08,e-.57))),
            Vector((x,-w,e-.18)),Vector((x,-w*.90,e)),Vector((x,-w*.58,h))]


rv=[section2(r) for r in RINGS]
verts=[tuple(v) for rr in rv for v in rr];faces=[];N=12
faces.extend([tuple(range(N-1,-1,-1)),tuple((len(rv)-1)*N+i for i in range(N))])
for j in range(len(rv)-1):
    for k in range(N):faces.append((j*N+k,j*N+(k+1)%N,(j+1)*N+(k+1)%N,(j+1)*N+k))
group="SM_HullStructure"
mesh("New continuous wedge pressure frame",verts,faces,FRAME,.035)


def hull_top(x,y):
    j=max(0,min(len(RINGS)-2,next((i for i in range(len(RINGS)-1) if RINGS[i][0]<=x<=RINGS[i+1][0]),len(RINGS)-2)))
    t=(x-RINGS[j][0])/(RINGS[j+1][0]-RINGS[j][0])
    row=[a.lerp(b,t) for a,b in zip(rv[j],rv[j+1])]
    yy=abs(y)
    if yy<=row[1].y:return row[1].z
    for k in (1,2,3):
        if yy<=row[k+1].y:
            q=(yy-row[k].y)/(row[k+1].y-row[k].y)
            return row[k].z*(1-q)+row[k+1].z*q
    return row[4].z


def surf(x,y,lift=0):return Vector((x,y,hull_top(x,y)+lift))


group="SM_HullCeramic"
for j in range(len(rv)-1):
    for k in range(N):
        # Deliberate cockpit cutout and two broad, inset radiator regions.
        if k==0 and j>=4:continue
        if k in (2,10) and j in (1,2,3):continue
        p=[rv[j][k],rv[j+1][k],rv[j+1][(k+1)%N],rv[j][(k+1)%N]]
        if k in (4,5,6,7,8):
            mat=BLACK if k in(5,6,7) else CERAMIC
            armor("Ventral layered shell",p,mat,.036,.055,False,False)
        else:
            mat=CERAMIC_COLD if (j+k)%11==0 else CERAMIC_WARM if (j+k)%7==0 else CERAMIC
            if k in(1,3,9,11) and j in(2,4,5,6):
                # A few long structural diagonals, not a repeated checkerboard.
                armor("Triangulated shoulder armor A",p[:3],mat,force_up=False)
                armor("Triangulated shoulder armor B",[p[0],p[2],p[3]],mat,force_up=False)
            else:armor("Contoured major armor plate",p,mat,force_up=False)

# The side seam is a recessed structural ledge, framed by a thin sacrificial rail.
group="SM_HullStructure"
for s in (-1,1):
    rail=[(r[0],s*(r[1]+.025),r[4]-.29) for r in RINGS]
    tube("Continuous perimeter protection rail",rail,.044,TITAN)
    if DETAIL:
        for zoff in (.10,-.08):
            tube("Recessed hull systems trunk",[(r[0],s*(r[1]-.015),r[4]-.42+zoff) for r in RINGS[:-1]],.028,ETCH)
        for i in range(len(RINGS)-1):
            x=(RINGS[i][0]+RINGS[i+1][0])/2;y=s*(RINGS[i][1]+RINGS[i+1][1])/2
            z=(RINGS[i][4]+RINGS[i+1][4])/2-.29
            box("Perimeter rail retaining saddle",(x,y,z),(.08,.11,.22),TITAN,.018)


def grille(name,xy,rows=30,crosses=13):
    p=[surf(x,y,.07) for x,y in xy]
    n=(p[1]-p[0]).cross(p[2]-p[0]).normalized()
    if n.z<0:n=-n
    p=[q+n*.07 for q in p]
    plate(name+" thermal recess floor",[q-n*.15 for q in p],BLACK,.04,.01)
    # Sloped recess walls and a narrow ivory lip form an actual recessed opening.
    for i in range(4):
        plate(name+" recess wall",[p[i],p[(i+1)%4],p[(i+1)%4]-n*.16,p[i]-n*.16],TITAN,.025,.006)
        beam(name+" ceramic perimeter",p[i],p[(i+1)%4],.13,CERAMIC,.15)
        for t in (.15,.50,.85):bolt(p[i].lerp(p[(i+1)%4],t)+n*.076,n,.017)
    for i in range(1,rows):
        t=i/rows
        a=p[0].lerp(p[1],t)-n*.040;b=p[3].lerp(p[2],t)-n*.040
        beam(name+" folded heat fin",a,b,.018,TITAN,.045)
        if DETAIL:
            # Continuous folded coolant channels, with tied ends at the manifolds.
            pts=[]
            for k in range(9):
                q=a.lerp(b,k/8)+Vector((.035 if k%2 else -.035,0,0))-n*.025
                pts.append(q)
            tube(name+" serpentine coolant pipe",pts,.011,SILVER)
    if DETAIL:
        for j in range(1,crosses):
            t=j/crosses
            tube(name+" wire protection lattice",[p[0].lerp(p[3],t)+n*.015,p[1].lerp(p[2],t)+n*.015],.009,TITAN)


for s,side in ((-1,"Port"),(1,"Starboard")):
    group="SM_Radiator"+side
    # Compact radiators are canted into the sculpted shoulders, never upright fins.
    grille("Aft shoulder radiator",[(-5.80,s*3.92),(-2.55,s*3.82),(-2.55,s*5.47),(-5.80,s*5.62)],36 if DETAIL else 14,16)
    grille("Forward shoulder radiator",[(-1.91,s*3.61),(.98,s*3.31),(1.49,s*4.82),(-1.65,s*5.32)],32 if DETAIL else 12,16)
    group="SM_HullCeramic"
    for xy in ([(-6.35,s*3.73),(-5.94,s*3.74),(-5.94,s*5.72),(-6.40,s*5.53)],
               [(-2.4,s*3.73),(-2.1,s*3.67),(-1.83,s*5.41),(-2.4,s*5.49)],
               [(1.1,s*3.24),(2.70,s*2.95),(2.63,s*4.40),(1.55,s*4.76)]):
        armor("Angular radiator shoulder surround",[surf(x,y,.12) for x,y in xy],CERAMIC)

# Deep twin engines. The large rings are actual sleeves around a hollow chamber.
engine_pivots={"SM_EnginePort":(-8.25,-4.40,0),"SM_EngineStarboard":(-8.25,4.40,0)}
for s,side in ((-1,"Port"),(1,"Starboard")):
    y=s*4.4;group="SM_Engine"+side
    annulus("Hollow primary nozzle liner",-8.76,-11.91,y,0,.58,1.34,.49,1.25,COPPER,128)
    cylinder("Dark combustion throat",(-8.735,y,0),(-8.75,y,0),.50,BLACK,96)
    for x,r,w,m in ((-8.64,1.14,.12,TITAN),(-9.04,1.27,.15,HEAT_BLUE),(-9.46,1.30,.18,TITAN),
                    (-10.02,1.34,.13,SILVER),(-10.57,1.38,.20,TITAN),(-11.15,1.43,.15,HEAT_BLUE),(-11.72,1.445,.26,TITAN)):
        annulus("Machined engine annular sleeve",x-w/2,x+w/2,y,0,r,r,r-.080,r-.080,m,128 if DETAIL else 64)
        ring("Machined engine sleeve edge",(x-w/2,y,0),r-.015,.028,SILVER,(1,0,0),128 if DETAIL else 64)
    ring("Engine exit replaceable lip",(-11.976,y,0),1.404,.024,SILVER,(1,0,0),128)
    if DETAIL:
        for j in range(12):
            t=j/11;x=-9.20-2.53*t;r=.65+.60*t
            ring("Internal heat exchanger rib",(x,y,0),r,.018,COPPER,(1,0,0),128)
        for k in range(40):
            a=2*math.pi*k/40
            pts=[]
            for j in range(9):
                t=j/8;theta=a+.075*math.sin(math.pi*t);r=1.05+.21*math.sin(math.pi*t)
                pts.append((-8.62-2.51*t,y+r*math.cos(theta),r*math.sin(theta)))
            tube("Engine braided axial cooling feed",pts,.030,SILVER if k%5 else AMBER)
            for xx,rr in ((-8.71,1.155),(-9.95,1.35),(-11.12,1.442)):
                normal=Vector((0,math.cos(a),math.sin(a)))
                bolt((xx,y+rr*normal.y,rr*normal.z),normal,.021)
        for k in range(24):
            a0=2*math.pi*k/24+.008;a1=2*math.pi*(k+1)/24-.008
            curved_band("Segmented exhaust heat shield",-11.56,-11.22,y,0,1.45,a0,a1,TITAN if k%5 else HEAT_BLUE,steps=8)
            curved_band("Segmented forward thermal blanket",-10.49,-10.22,y,0,1.39,a0,a1,CERAMIC2 if k%3 else TITAN,steps=8)
        for k in range(16):
            a=2*math.pi*k/16
            yy=math.cos(a);zz=math.sin(a)
            beam("Radial engine frame link",(-8.26,y+yy*.72,zz*.72),(-9.13,y+yy*1.19,zz*1.19),.095,FRAME)
            cylinder("Engine gimbal actuator housing",(-8.32,y+yy*.9,zz*.9),(-8.90,y+yy*1.21,zz*1.21),.075,TITAN)
            cylinder("Engine gimbal polished rod",(-8.82,y+yy*1.17,zz*1.17),(-9.25,y+yy*1.24,zz*1.24),.038,SILVER)
            # Warm inner refractory vanes remain opaque solid geometry.
            a2=a+.18
            plate("Nozzle throat guide vane",[(-9.0,y+.56*yy,.56*zz),(-11.60,y+1.18*yy,1.18*zz),
                  (-11.60,y+1.18*math.cos(a2),1.18*math.sin(a2)),(-9.0,y+.56*math.cos(a2),.56*math.sin(a2))],COPPER,.017,.003)
    group="SM_Outrigger"+side
    # White dorsal buttress and diagonal metal interfaces blend into the hull.
    for yy in (-.76,.76):
        beam("Sculpted engine cradle upper spar",(-7.0,y+yy,1.36),(-8.99,y+yy*.75,.75),.26,TITAN,.19)
        beam("Sculpted engine cradle lower spar",(-7.2,y+yy,-1.12),(-8.75,y+yy*.75,-.65),.19,TITAN)
    armor("Engine dorsal ceramic cowl",[(-6.25,y-1.05,1.47),(-8.58,y-.69,1.21),(-8.58,y+.69,1.21),(-6.25,y+1.05,1.47)],CERAMIC,.02,.11)
    if DETAIL:
        for sy in (-1,1):
            for z in (-.40,-.05,.3):
                tube("Engine bay service harness",[(-6.38,y+sy*.81,z),(-7.08,y+sy*1.10,z),(-8.62,y+sy*1.02,z-.14),(-9.10,y+sy*.75,z-.12)],.042,RUBBER if z<0 else COPPER)

# The low canopy is part of the longitudinal wedge, not a box on the roof.
group="SM_CockpitShell"
box("Pressure cabin floor",(6.39,0,.055),(4.72,3.13,.20),FRAME,.065)
box("Low rear cabin bulkhead",(4.09,0,1.08),(.14,3.14,2.12),RUBBER,.055)
roof=[(3.44,-1.66,1.91),(5.43,-1.57,2.16),(5.43,1.57,2.16),(3.44,1.66,1.91)]
armor("Integrated swept canopy crown",roof,CERAMIC,.025,.13)
plate("Cockpit roof acoustic lining",[(3.53,-1.51,1.86),(5.38,-1.45,2.035),(5.38,1.45,2.035),(3.53,1.51,1.86)],RUBBER,.045,.022)
wind=[(5.44,-1.51,2.075),(10.67,-.855,.294),(10.67,.855,.294),(5.44,1.51,2.075)]
for s in (-1,1):
    outline=[(4.12,s*1.59,.34),(4.12,s*1.59,1.945),(5.44,s*1.56,2.14),(10.72,s*.90,.267)]
    for a,b in zip(outline,outline[1:]):beam("Swept canopy structural rail",a,b,.075,FRAME)
    beam("Canopy lower sill",outline[0],outline[-1],.078,FRAME)
    group="SM_CanopyGlass"
    plate("Long side quarterlight",[(4.16,s*1.575,.37),(10.54,s*.90,.30),(5.41,s*1.532,2.071),(4.16,s*1.575,1.914)],GLASS,.009,.002)
    group="SM_CockpitShell"
    plate("Opaque lower sill fairing",[(4.0,s*1.66,.15),(10.73,s*.97,.13),(10.73,s*.97,.31),(4.0,s*1.66,.47)],CERAMIC,.065,.014)
    # Sculpted cheeks taper into the nose outside the window.
    a=Vector((4.56,s*1.65,1.53));b=Vector((5.44,s*1.57,2.16))
    c=Vector((10.77,s*.99,.27));d=surf(9.9,s*2.06,.035);e=surf(7.25,s*3.30,.035)
    armor("Canopy cheek rear structural plane",[a,b,e],CERAMIC,.010,.09)
    armor("Canopy cheek forward armor plane",[b,c,d],CERAMIC,.010,.09)
    armor("Canopy cheek outer armor plane",[b,d,e],CERAMIC_WARM,.010,.09)
    if DETAIL:
        group="SM_HullMarkings"
        n=(b-a).cross(e-a).normalized()
        if n.z<0:n=-n
        u=(Vector((1,0,0))-n*n.x).normalized();v=n.cross(u)
        rotation=Matrix((u,v,n)).transposed().to_euler()
        label("Canopy cheek etched registration","ASTER 24",a*.24+b*.24+e*.52+n*.024,.16,TITAN,rotation,align="CENTER")
        group="SM_CockpitShell"
for i in range(4):beam("Laminated windscreen fine rim",wind[i],wind[(i+1)%4],.061,TITAN)
group="SM_CanopyGlass"
plate("Panoramic swept front laminate",wind,GLASS,.010,.002)

# Purposeful layered bow, inset optics and a replaceable lower impact jaw.
group="SM_SciencePayload"
armor("Tapered central sensor bridge",[(10.72,-.87,.27),(12.00,-.28,-.17),(12.00,.28,-.17),(10.72,.87,.27)],CERAMIC,.01,.065)
for s in (-1,1):
    # Real rectangular sensor pockets with a frame in front of recessed optics.
    box("Nose sensor recess",(11.65,s*.94,-.25),(.38,.63,.26),BLACK,.035)
    for yoff in (-.18,.18):
        cy=s*.94+yoff
        cylinder("Forward optical assembly",(11.77,cy,-.24),(11.91,cy,-.24),.093,TITAN,32)
        cylinder("Recessed front sapphire optic",(11.916,cy,-.24),(11.922,cy,-.24),.072,SCREEN,32)
        ring("Optical retaining bezel",(11.929,cy,-.24),.078,.01,SILVER,(1,0,0),32)
    beam("Sensor recess upper eyebrow",(11.99,s*.59,-.078),(11.99,s*1.29,-.078),.071,CERAMIC)
    beam("Sensor recess lower rail",(12.02,s*.59,-.431),(12.02,s*1.29,-.431),.055,TITAN)
    box("Side ranging aperture",(8.50,s*2.34,.26),(.72,.12,.18),SCREEN,.025)
group="SM_HullCeramic"
jaw=[(9.15,-2.44,-.73),(12.05,-1.38,-.71),(12.15,-1.20,-.50),(12.15,1.20,-.50),
     (12.05,1.38,-.71),(9.15,2.44,-.73)]
plate("Layered sacrificial bow jaw",jaw,CERAMIC2,.055,.014)
for s in (-1,1):
    beam("Bow replaceable edge insert",(10.06,s*2.0,-.58),(12.15,s*1.20,-.48),.041,SILVER)

# Dorsal servicing is concentrated at actual mechanical interfaces.
group="SM_ServiceModule"
armor("Recessed dorsal service plinth",[surf(-5.25,-1.5,.05),surf(-.65,-1.5,.05),surf(-.65,1.5,.05),surf(-5.25,1.5,.05)],TITAN,.03,.18)
armor("Pressure servicing hatch",[surf(-3.65,-1.26,.24),surf(-.98,-1.26,.24),surf(-.98,.98,.24),surf(-3.65,.98,.24)],CERAMIC,.018,.09)
box("Hatch locking bar",(-2.24,-.08,2.155),(1.13,.12,.067),TITAN,.014)
for y in (-1.48,1.48):
    tube("Protected dorsal power trunk",[(-7.47,y,1.45),(-5.75,y,1.83),(-3.70,y,2.03),(-.55,y,1.92),(1.20,y,1.9)],.085,RUBBER)
    tube("Dorsal coolant return",[(-7.40,y+.18,1.50),(-5.65,y+.18,1.83),(-3.68,y+.18,2.01),(.74,y+.18,1.94)],.042,SILVER)
if DETAIL:
    for x in (-6.10,-5.3,-4.50,-3.7,-2.90,-2.1,-1.3):
        for y in (-1.79,1.79):
            z=hull_top(x,y)+.09
            box("Dorsal line-replaceable power interface",(x,y,z),(.67,.31,.20),CERAMIC2,.035)
            box("Power interface connector recess",(x,y-.165,z),(.40,.033,.095),BLACK,.009)
            for dx in (-.21,.21):bolt((x+dx,y,z+.104),radius=.017)
            if int(abs(x)*10)%3==0:box("Power interface amber latch",(x,y+.17,z),(.13,.04,.055),AMBER,.009)
    for x in (-5.2,-4.45,-3.7,-2.95,-2.2,-1.45):
        for y in (-1.5,1.5):
            box("Dorsal trunk retaining clamp",(x,y,2.08),(.08,.32,.16),TITAN,.018)
    for x,y in ((-6.9,-.69),(-6.9,.69),(1.45,-.58),(1.45,.58)):
        z=hull_top(x,y)+.065
        box("Dorsal louver pocket",(x,y,z),(1.13,.63,.06),FRAME,.025)
        for i in range(13):box("Dorsal louver leaf",(x-.5+i*.083,y,z+.055),(.027,.52,.045),TITAN,.008)
    for x,y in ((-4.52,.19),(-4.52,.96),(2.55,-.32)):
        z=hull_top(x,y)+.20
        cylinder("Dorsal valve access",(x,y,z),(x,y,z+.07),.20,TITAN,48)
        ring("Valve access clamp",(x,y,z+.075),.155,.018,SILVER,(0,0,1),48)
    for x,y in ((-1.15,2.15),(1.28,-2.1)):
        z=hull_top(x,y)
        cylinder("Navigation mast foot",(x,y,z),(x,y,z+.12),.10,TITAN)
        beam("Compact navigation aerial",(x,y,z+.10),(x-.07,y,z+.66),.023,SILVER)
        sphere("Aerial dielectric endcap",(x-.07,y,z+.66),(.045,.035,.043),CERAMIC)

# Aft firewall couplings remain visible between the two engines in chase view.
group="SM_AftServiceBulkhead"
for y in (-1.36,0,1.36):
    box("Aft servicing panel",(-8.59,y,-.04),(.10,1.02,1.35),TITAN,.09)
    box("Aft black service pocket",(-8.657,y,-.03),(.065,.84,1.09),FRAME,.04)
    for z in (-.26,.27):
        cylinder("Aft sealed umbilical",(-8.69,y,z),(-8.79,y,z),.16,SILVER,48)
        cylinder("Umbilical dust cover",(-8.795,y,z),(-8.803,y,z),.12,BLACK,48)
    if DETAIL:
        for yy in (-.40,.40):
            for zz in (-.55,.55):bolt((-8.72,y+yy,zz),(-1,0,0),.025)
for s in (-1,1):
    tube("Aft systems manifold",[(-8.57,s*2.08,-.88),(-8.79,s*2.08,-.4),(-8.79,s*2.45,.75),(-8.54,s*2.7,1.05)],.086,TITAN)

# Authored articulated landing legs: the contract is unchanged from v1.
gear_pivots={"SM_GearNose":(6.1,0,-.90),"SM_GearPort":(-4.4,-3.1,-.83),"SM_GearStarboard":(-4.4,3.1,-.83)}
feet={"SM_GearNose":(7,0,-4),"SM_GearPort":(-5,-4.8,-4),"SM_GearStarboard":(-5,4.8,-4)}
for name,pivot0 in gear_pivots.items():
    group=name;pivot=Vector(pivot0);foot=Vector(feet[name]);knee=pivot.lerp(foot,.62)+Vector((-.30,0,.06))
    box("Landing trunnion yoke",pivot,(.68,.84,.43),FRAME,.065)
    cylinder("Landing trunnion axle",pivot+Vector((0,-.5,0)),pivot+Vector((0,.5,0)),.20,SILVER,48)
    for sy in (-.23,.23):
        beam("Forged landing upper fork",pivot+Vector((0,sy,-.10)),knee+Vector((0,sy,0)),.18,TITAN,.26)
        beam("Triangulated drag link",pivot+Vector((-.73,sy,-.01)),knee+Vector((0,sy,.18)),.082,SILVER)
        beam("Lower landing suspension blade",knee+Vector((.17,sy,0)),foot+Vector((0,sy,.34)),.085,TITAN,.14)
    cylinder("Landing knee bearing",knee+Vector((0,-.38,0)),knee+Vector((0,.38,0)),.20,SILVER,48)
    cylinder("Oleo pressure housing",knee,foot+Vector((0,0,.68)),.215,TITAN,48)
    cylinder("Chrome landing piston",foot+Vector((0,0,.81)),foot+Vector((0,0,.30)),.112,SILVER,48)
    if DETAIL:
        for i in range(10):
            q=knee.lerp(foot+Vector((0,0,.67)),i/11)
            ring("Oleo heat and debris protection ring",q,.213,.020,FRAME,(foot-knee).normalized(),48)
        for sy in (-.32,.32):
            pts=[pivot+Vector((.16,sy,-.12)),pivot.lerp(knee,.4)+Vector((.23,sy,0)),knee+Vector((.22,sy,.09)),foot+Vector((.20,sy*.65,.48))]
            tube("Landing hydraulic braided line",pts,.025,RUBBER)
            for j in range(1,4):
                q=knee.lerp(foot,j/5)+Vector((.0,sy,.06))
                bolt(q,(0,1 if sy>0 else -1,0),.026)
        for sy in (-.44,.44):
            normal=(0,1 if sy>0 else -1,0)
            bolt(knee+Vector((0,sy,0)),normal,.11)
    # Octagonal feet with a real flat sole exactly on z=-4 m.
    shoe=[(-.61,-.49),(.53,-.49),(.68,-.34),(.68,.34),(.53,.49),(-.61,.49),(-.70,.32),(-.70,-.32)]
    plate("Articulated machined landing shoe",[(foot.x+x,foot.y+y,foot.z+.28) for x,y in shoe],TITAN,.235,.055)
    plate("Flat sacrificial landing sole",[(foot.x+x,foot.y+y,foot.z+.045) for x,y in shoe],BLACK,.045,.012)
    cylinder("Foot articulation pin",foot+Vector((0,-.34,.36)),foot+Vector((0,.34,.36)),.12,SILVER,48)
    for dx in (-.43,.43):
        for dy in (-.32,.32):bolt(foot+Vector((dx,dy,.286)),radius=.033)
    group="SM_GearBays"
    bayz=-.82 if "Nose" in name else -1.055
    for sy in (-.58,.58):
        box("Inset landing bay armored surround",(pivot.x,pivot.y+sy,bayz),(1.86,.14,.12),CERAMIC,.022)
        box("Landing door inner hinge",(pivot.x,pivot.y+sy,bayz-.10),(1.79,.065,.10),TITAN,.016)

group="SM_Heatshield"
for x in (-6.0,-4.9,-3.8,-2.7,-1.6,-.5,.6,1.7,2.8):
    for y in (-2,-1,0,1,2):
        z=-1.115 if x<1 else -1.01
        box("Ventral ceramic service tile",(x,y,z),(1.01,.91,.04),BLACK,.012)
for s in (-1,1):
    tube("Ventral armored fluid manifold",[(-7.7,s*2.6,-1.04),(-5.9,s*2.6,-1.13),(1.9,s*2.6,-1.06),(4.1,s*2.4,-.86)],.065,TITAN)
    if DETAIL:
        for x in (-5.8,-4.2,-2.6,-1.0,.6,2.0):
            box("Ventral manifold anchor",(x,s*2.6,-1.07),(.095,.24,.12),FRAME,.016)
box("Ventral science optics cassette",(.55,0,-1.23),(1.85,1.25,.20),TITAN,.06)
box("Ventral scanning window",(.55,0,-1.35),(1.52,.94,.045),SCREEN,.025)

# RCS manifolds are recessed into the hull's four corners, with visible nozzles.
rcs_manifest=[]
for x,xname,half in ((8.6,"Fore",2.48),(-6.86,"Aft",6.12)):
    for s,side in ((-1,"Port"),(1,"Starboard")):
        y=s*half;z=.07 if x>0 else .33;group="SM_RCS_"+xname+side
        box("Recessed RCS manifold",(x,y,z),(.48,.34,.42),FRAME,.043)
        for i,(axis,off) in enumerate((((0,s,0),(0,s*.14,0)),((0,0,1),(0,0,.18)),((0,0,-1),(0,0,-.18)),((1 if x>0 else -1,0,0),(.21 if x>0 else -.21,0,0)))):
            base=Vector((x,y,z))+Vector(off)
            nozzle("RCS attitude nozzle",base,axis,.108,.215)
            rcs_manifest.append({"name":group+"_"+str(i),"position_m":list(base),"exhaust_axis":list(axis)})

# The dedicated cockpit file builds real seats, instruments and named controls at
# unchanged runtime sockets. It does not display invented live navigation values.
exec(compile((ROOT/"Tools/Art/explorer_v2_cockpit.py").read_text(encoding="utf-8"),"explorer_v2_cockpit.py","exec"),globals())

group="SM_HullMarkings"
if DETAIL:
    label("Aft service identity","ASTER / 024",(-8.82,.71,.88),.12,WHITE,(math.pi/2,0,-math.pi/2))
    for s in (-1,1):
        label("Shoulder registration","ASTER 24",(2.8,s*3.55,hull_top(2.8,s*3.55)+.07),.18,TITAN)
        for x,y in ((-5.9,s*2.6),(-.6,s*2.25),(6.7,s*2.13)):
            z=hull_top(x,y)+.067
            box("Restrained amber access identifier",(x,y,z),(.27,.12,.009),AMBER,.006)
            label("Access number","03",(x-.068,y-.035,z+.007),.064,BLACK)
    for x,y in ((-3.3,-.9),(1.1,.52)):
        label("Etched servicing legend","SERVICE / PRESSURE",(x,y,hull_top(x,y)+.21),.074,TITAN)

print("STAR V2 new wedge/canopy/shoulder/engine geometry complete",flush=True)
