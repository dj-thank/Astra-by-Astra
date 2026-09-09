"""Dedicated V2 cockpit and exact live-display planes."""
# Comfortable dual-seat cockpit, aft stowage, floor grids, overhead and side panels.
group="SM_CockpitInterior"
for y in (-.62,.79):
    box("Seat pedestal",(5.06,y,.37),(.63,.72,.44),FRAME,.055)
    box("Seat cushion",(5.28,y,.67),(.94,.68,.17),FABRIC,.11)
    back=box("Contoured seat back",(4.78,y,1.08),(.18,.71,.91),FABRIC,.11)
    back.rotation_euler.y=-.14
    box("Head restraint",(4.70,y,1.65),(.21,.48,.29),FABRIC,.085)
    for sy in (-.37,.37):
        beam("Seat support rail",(4.76,y+sy,.37),(4.63,y+sy,1.66),.068,SILVER)
        box("Padded armrest",(5.2,y+sy,.97),(.55,.09,.13),RUBBER,.035)
    for sy in (-.15,.15):
        beam("Five point harness webbing",(4.88,y+sy,1.42),(5.12,y+sy*.45,.78),.049,AMBER,.012)
    box("Harness buckle",(5.17,y,.78),(.09,.1,.055),SILVER,.018)
for x in (4.53,5.00,5.47,5.94,6.41,6.88,7.35,7.82):
    for y in (-1.12,-.55,0,.55,1.12):
        box("Cockpit anti-slip floor grid",(x,y,.167),(.39,.42,.016),RUBBER,.008)
for y in (-1.27,1.27):
    box("Side console",(6.33,y,.57),(2.5,.38,.68),RUBBER,.095)
    box("Side console metal lip",(6.33,y,.93),(2.47,.39,.055),TITAN,.017)
    for x in (5.55,5.84,6.13,6.42,6.71,7.0):
        box("Side switch inset",(x,y,.967),(.22,.23,.016),FRAME,.012)
        cylinder("Toggle spindle",(x,y,.98),(x+.025,y,1.026),.018,SILVER,12)
    for x in (5.25,7.35): cylinder("Console rotary control",(x,y,.972),(x,y,1.04),.051,TITAN,24)
    tube("Cockpit grab rail",[(5.1,y*.965,1.11),(5.2,y*.965,1.2),(5.65,y*.965,1.2),(5.75,y*.965,1.11)],.028,SILVER)
box("Rear service door",(4.279,0,1.03),(.029,.70,1.38),TITAN,.06)
box("Rear door pull",(4.32,.23,1.05),(.071,.055,.25),AMBER,.015)
for y in (-1.19,1.19):
    box("Emergency stowage",(4.32,y,1.16),(.25,.43,.70),CERAMIC2,.065)
    box("Stowage latch",(4.46,y,1.17),(.03,.22,.06),AMBER,.016)
for y in (-1.1,1.1):
    box("Overhead reading light",(5.55,y,1.997),(.5,.05,.016),LIGHT,.006)
box("Overhead console",(4.91,0,1.97),(1.58,.70,.12),FRAME,.04)
for x in (4.40,4.70,5.00,5.30):
    for y in (-.18,.18): box("Overhead guarded switch",(x,y,1.89),(.12,.1,.08),TITAN,.009)

# Three actual front display surfaces, with individually named UI anchor empties.
# These are authored fixed bezel markings; no live telemetry is fabricated.
screen_sockets={}
group="SM_InstrumentPanel"
box("Main instrument console",(7.22,0,.53),(.59,2.66,.61),RUBBER,.10)
for i,y in enumerate((-.88,0,.88)):
    box("Display bezel",(6.894,y,.73),(.055,.76,.45),TITAN,.035)
    box("Display gasket",(6.858,y,.745),(.019,.681,.371),BLACK,.018)
    o=box("Display writable surface "+str(i),(6.843,y,.747),(.006,.619,.312),SCREEN,.009)
    screen_sockets["SOCKET_Display"+str(i)]={"location_m":[6.838,y,.747],"center_m":[6.838,y,.747],
        "normal":[-1,0,0],"size_m":[.619,.312],"width_m":.619,"height_m":.312,
        "basis":{"normal":[-1,0,0],"right":[0,-1,0],"up":[0,0,1]},
        "widget_rotation_ue_deg_if_local_normal_positive_x":{"pitch":0,"yaw":180,"roll":0},
        "surface_offset_m":.002}
    # Text objects face rearward (-X), keeping artwork separate from runtime data.
    label("Fixed display function",("NAVIGATION","ATTITUDE","SCIENCE")[i],(6.824,y+.27,.895),.047,WHITE,(math.pi/2,0,-math.pi/2))
    for by in (-.27,-.18,-.09,0,.09,.18,.27):
        box("Display soft key",(6.827,y+by,.534),(.025,.047,.025),RUBBER,.005)
    for by in (-.343,.343):
        cylinder("Display encoder",(6.816,y+by,.61),(6.767,y+by,.61),.029,SILVER,20)
    # Quiet power/status LEDs, no fictitious numbers in delivered art.
    box("Display power LED",(6.82,y-.30,.91),(.009,.014,.009),CYAN,.002)
label("Console maker badge","STAR / ASTER 24",(6.84,.28,.449),.041,WHITE,(math.pi/2,0,-math.pi/2))

# Stick and throttle are dedicated objects with real pivots for input animation.
group="SM_ControlStick"
stick_pivot=(5.92,-.41,.57)
sphere("Stick gaiter",stick_pivot,(.13,.13,.085),RUBBER)
tube("Flight stick stem",[(5.92,-.41,.57),(5.93,-.41,.79),(5.88,-.41,.98)],.032,TITAN)
grip=box("Ergonomic flight stick",(5.87,-.41,1.016),(.13,.096,.22),RUBBER,.042)
grip.rotation_euler.y=-.13
box("Stick thumb hat",(5.81,-.395,1.12),(.04,.045,.022),TITAN,.009)
box("Scan trigger",(5.946,-.409,1.04),(.025,.051,.055),AMBER,.009)
group="SM_Throttle"
throttle_pivot=(5.90,-1.21,.97)
box("Throttle quadrant fixed top",throttle_pivot,(.58,.23,.035),FRAME,.03)
beam("Throttle lever",throttle_pivot,(5.99,-1.21,1.16),.037,TITAN)
box("Throttle palm grip",(5.99,-1.21,1.16),(.105,.20,.072),RUBBER,.028)
group="SM_CockpitInterior"
for y in (-.28,.28):
    o=box("Rudder pedal",(6.53,y,.34),(.21,.20,.055),TITAN,.02)
    o.rotation_euler.y=-.4
    for yy in (-.066,0,.066): box("Pedal tread",(6.54,y+yy,.375),(.13,.012,.018),RUBBER,.003)
