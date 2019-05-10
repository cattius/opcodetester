import xml.parsers.expat as expat
import re as regex
import json
import os
from z3 import *
from sets import Set
# Running this example on Linux (may or may not be needed, I can run it fine without atm):
# export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:MYZ3/bin
# export PYTHONPATH=MYZ3/bin/python
# python example.py

architectures = {}   #a dict of dicts (of port uop counts with attrib. name = 'instrname')
#architectures are NHM, WSM, SNB, IVB, HSW, BDW, SKL, SKX, KBL, CFL
#ignore extensions - assume ext is not supported by arch if we don't have a measurement for it
instructions = {}

currentInstr = None
currentArch = None
def get_arches(name, attr):
    global currentInstr
    global currentArch
    if name == 'instruction':
        currentInstr = {
        'name' : str(attr['string']),
        'category' : str(attr['category']),
        'extension' : str(attr['extension']),
        'iclass' : str(attr['iclass']),
        'iform' : str(attr['iform']),
        'isa-set' : str(attr['isa-set'])
        }
        instructions[str(attr['string'])] = currentInstr

    if name == 'architecture':
        if attr['name'] not in architectures:
            architectures[attr['name']] = []
        currentArch = attr['name']
    if name == 'measurement':
        #ignore IACA values and just use their HW measurements as they found mistakes in IACA's reported values.
        #unless SKX - no HW measurements, dealt with in separate function below
        #max num of ports I'm aware of is Skylake 8 (incl. derivatives like CFL)
        stats = {
        'instr': currentInstr['name'],
        'p0' : attr.get('port0', '0'),
        'p1' : attr.get('port1', '0'),
        'p2' : attr.get('port2', '0'),
        'p3' : attr.get('port3', '0'),
        'p4' : attr.get('port4', '0'),
        'p5' : attr.get('port5', '0'),
        'p6' : attr.get('port6', '0'),
        'p7' : attr.get('port7', '0'),
        'portCombos': []
        }
        for key, value in attr.items():
            if(regex.match('port\d\d+', key) != None):
                portCombo = []
                #Python list ordering is persistent so we can assume value will always be at index 0
                portCombo.append(value)
                ports = regex.split('port', key, maxsplit=0)
                for c in range(len(ports[1])):
                    port = ports[1][c]
                    if(regex.match('\d', port) != None):
                        portCombo.append(port)
                stats['portCombos'].append(portCombo)

            elif(regex.match('port\d', key) != None):
                port = regex.split('port', key, maxsplit=0)
                stats['p' + str(port[1])] = value

        architectures[currentArch].append(stats)



currentVersion = 0
def get_skx(name, attr):
    global currentInstr
    global currentArch
    global currentVersion
    if name == 'instruction':
        currentVersion = 0
        currentInstr = str(attr['string'])
    if name == 'architecture':
        currentArch = attr['name']
    if name == 'IACA' and currentArch == 'SKX' and attr['version'] > currentVersion:
        currentVersion = attr['version']
        stats = {
        'instr': currentInstr,
        'p0' : attr.get('port0', '0'),
        'p1' : attr.get('port1', '0'),
        'p2' : attr.get('port2', '0'),
        'p3' : attr.get('port3', '0'),
        'p4' : attr.get('port4', '0'),
        'p5' : attr.get('port5', '0'),
        'p6' : attr.get('port6', '0'),
        'p7' : attr.get('port7', '0'),
        'portCombos': []
        }
        for key, value in attr.items():
            if(regex.match('port\d\d+', key) != None):
                portCombo = []
                #Python list ordering is persistent so we can assume value will always be at index 0
                portCombo.append(value)
                ports = regex.split('port', key, maxsplit=0)
                for c in range(len(ports[1])):
                    port = ports[1][c]
                    if(regex.match('\d', port) != None):
                        portCombo.append(port)
                stats['portCombos'].append(portCombo)

            elif(regex.match('port\d', key) != None):
                port = regex.split('port', key, maxsplit=0)
                stats['p' + str(port[1])] = value
        architectures[currentArch].append(stats)

comboVals = Set()
p0 = Int('p0')
p1 = Int('p1')
p2 = Int('p2')
p3 = Int('p3')
p4 = Int('p4')
p5 = Int('p5')
p6 = Int('p6')
p7 = Int('p7')

#"get constraint" helper function
#return instruction value if necessary for constraint, else return 0
def gc(val, portStr):
    global comboVals
    if portStr in comboVals:
        return val
    else:
        return 0

def gcbool(val, portStr):
    global comboVals
    if portStr in comboVals:
        return True
    else:
        return False

def gczero():
    if not (gcbool(p0, "0") or gcbool(p1, "1") or gcbool(p2, "2") or gcbool(p3, "3") or gcbool(p4, "4") or gcbool(p5, "5") or gcbool(p6, "6") or gcbool(p7, "7")):
        return True
    else:
        return False

'''def getConstraint(portCombo, instr):
    global comboVals
    comboVals.clear()
    for port in portCombo[1:]:
        comboVals.add(port)
    left = ( gc(p0, "0") + gc(p1, "1") + gc(p2, "2"), + gc(p3, "3") + gc(p4, "4") + gc(p5, "5") + gc(p6, "6") + gc(p7, "7") )
    right = ( gc(int(instr['p0']), "0") + gc(int(instr['p1']), "1") + gc(int(instr['p2']), "2") + gc(int(instr['p3']), "3") + gc(int(instr['p4']), "4") + gc(int(instr['p5']), "p5")
    + gc(int(instr['p6']), 'p6') + gc(int(instr['p7']), 'p7') + int(portCombo[0]) )
    return left, right '''

    #combos = xyz 10, abc 12, xy 3
    #constraints:
        # x + y + z == instr(x) + instr(y) + instr(z) + 10
        # a + b + c == instr(a) + instr(b) + instr(c) + 12
        # THEN check for ports contained in multiple combos
        # x + y == instr(x) + instr(y) + 13

'''def getMultiConstraint(portNum, portCombos, instr):
    global comboVals
    comboVals.clear()
    offset = 0
    for combo in portCombos:
        if str(portNum) in combo[1:]:
            offset += int(combo[0])
            for value in combo[1:]:
                comboVals.add(value)
    left = ( gc(p0, "0") + gc(p1, "1") + gc(p2, "2"), + gc(p3, "3") + gc(p4, "4") + gc(p5, "5") + gc(p6, "6") + gc(p7, "7") )
    right = ( gc(int(instr['p0']), "0") + gc(int(instr['p1']), "1") + gc(int(instr['p2']), "2") + gc(int(instr['p3']), "3") + gc(int(instr['p4']), "4") + gc(int(instr['p5']), "p5")
    + gc(int(instr['p6']), 'p6') + gc(int(instr['p7']), 'p7') + offset )
    return left, right '''

def notAComboPort(port, portCombos):
    portStr = str(port)
    for portCombo in portCombos:
        if portStr in portCombo:
            return False
    return port

def main():

    makeDatafiles = False
    interpretData = True
    if(makeDatafiles):
        p = expat.ParserCreate()
        p.StartElementHandler = get_arches
        with open('instructions.xml', 'r') as f:
            p.ParseFile(f)
        with open('instructions.xml', 'r') as f:
            p = expat.ParserCreate()
            p.StartElementHandler = get_skx
            p.ParseFile(f)
        for arch, instrs in architectures.items():
            filename = arch + ".txt"            #open file with name of arch
            with open(filename, 'w') as f:
                json.dump(instrs, f)

        with open('instrs.txt', 'w') as f:
            json.dump(instructions, f)


    #result = [1,0,0,0,0,0,1,0]  #ADD  1*p0156+1*p06
    #result = [78742, 113334, 0, 0, 132280, 38340, 55310, 0]
    #result = [0,0,0,0,0,0,0,0]
    #result = [1*p0+6*p01+39*p06+5*p1+13*p15+4*p5
    #result = [7, 5, 0, 0, 0, 17, 39, 0]
    #{"p2": "0", "p3": "0", "p0": "1", "p1": "5", "p6": "0", "p7": "0", "p4": "0", "instr": "RDMSR", "portCombos": [["39", "0", "6"], ["6", "0", "1"], ["13", "1", "5"]], "p5": "4"}
    result = [7, 18, 0, 0, 0, 4, 39, 0]
    arch = 'BDW'

    if interpretData:
        filename = arch + ".txt"            #open file with name of arch
        with open(filename, 'r') as f:
            instrs = json.load(f)
        with open('instrs.txt', 'r') as f:
            instrDetails = json.load(f)

            s = Solver()
            p0 = Int('p0')
            p1 = Int('p1')
            p2 = Int('p2')
            p3 = Int('p3')
            p4 = Int('p4')
            p5 = Int('p5')
            p6 = Int('p6')
            p7 = Int('p7')
            #ok as a start let's try and make some upper and lower bounds
            #add result values
            s.add(p0 == 7),
            s.add(p1 == 18),
            s.add(p2 == 0),
            s.add(p3 == 0),
            s.add(p4 == 0),
            s.add(p5 == 4),
            s.add(p6 == 39),
            s.add(p7 == 0),
            #combo constraints
            #p0 = 1 + (39-p6) + (6-p1)
            #p0 = certain value + (combo val - other combo ports) + (combo val - other combo ports)
            #I can see how I could automate this!!!
            #p1 = 5 + (13-p5) + (6-p0)
            #p5 = 4 + (13-p1)
            #ok this works, so how can I derive these constrains automatically?
            input = json.loads('{"p2": "0", "p3": "0", "p0": "1", "p1": "5", "p6": "0", "p7": "0", "p4": "0", "instr": "RDMSR", "portCombos": [["39", "0", "6"], ["6", "0", "1"], ["13", "1", "5"]], "p5": "4"}')
            pmap = {"p0" : p0, "p1" : p1, "p2" : p2, "p3" : p3, "p4" : p4, "p5" : p5, "p6" : p6, "p7" : p7}
            imap = dict([(0, p0), (1, p1), (2, p2), (3, p3), (4, p4), (5, p5), (6, p6), (7, p7)])
            map = {"0" : p0, "1" : p1, "2" : p2, "3" : p3, "4" : p4, "5" : p5, "6" : p6, "7" : p7}
            portCombos = input["portCombos"]

            #solution: make temporary variables!!! and constraints are sums of all temporary variables??
            '''
            so in this case,
            max = If(x > y, x, y)


            '''

            portVals = []
            portMentions =[ [], [], [], [], [], [], [], [] ]
            for i in range(8):
                #find all mentions of that port (and other ports linked with)
                    #check its px value

                    portVals.append()
                    #check combo ports
                    for port in portCombos:
                        if str(i) in port[1:]:
                            portMentions[i].append( (port[0], [minus ports]) )
                #turn list of mentions into an inequality



            for i in range(8):
                if(notAComboPort(i, portCombos)):
                    s.add(imap[])
                    #add certain value
                else:
                    #find all instances of that value in combos
                    #add together first items of combo lists, this is our upper bound
                    #lower bound is the p{i} value

            #p0
            #offset = input["p0"]
            #for combo in portCombos:
            #    if "0" in combo[:1]:
        #            for port in combo[:1]:
                        #build constraint, remember to exclude 0

            #p0 + p6 == 1,
            #p0 + p1 + p6 == 51,
            #p0 + p1 == 6,
            #p0 + p1 + p5 == 25,
            #p1 + p5 == 5,

            print(s.check())
            print(s.model())
            return

            #TODO handle cases where arch doesn't have 8 ports (both here and above!)

            match = False
            #optimization
            if(all(i == 0 for i in result)):
                print("Matches instruction: NOP")
                match = True
            else:
                for instr in instrs:
                    portCombos = instr['portCombos']

                    #all port values must match the actual values we're checking against
                    s.add(p0 == result[0], p1 == result[1], p2 == result[2], p3 == result[3], p4 == result[4], p5 == result[5], p6 == result[6], p7 == result[7])

                    offsets = [0, 0, 0, 0, 0, 0, 0]
                    #if not in port combos, must equal value given for port; else take given value into account when checking combo constraints
                    if(notAComboPort(0, portCombos)):
                        s.add ( p0 == int(instr['p0']) )
                    else:
                        offsets[0] = int(instr['p0'])

                    if(notAComboPort(1, portCombos)):
                        s.add ( p1 == int(instr['p1']) )
                    else:
                        offsets[1] = int(instr['p1'])

                    if(notAComboPort(2, portCombos)):
                        s.add ( p2 == int(instr['p2']) )
                    else:
                        offsets[2] = int(instr['p2'])

                    if(notAComboPort(3, portCombos)):
                        s.add ( p3 == int(instr['p3']) )
                    else:
                        offsets[3] = int(instr['p3'])

                    if(notAComboPort(4, portCombos)):
                        s.add ( p4 == int(instr['p4']) )
                    else:
                        offsets[4] = int(instr['p4'])

                    if(notAComboPort(5, portCombos)):
                        s.add ( p5 == int(instr['p5']) )
                    else:
                        offsets[5] = int(instr['p5'])

                    if(notAComboPort(6, portCombos)):
                        s.add ( p6 == int(instr['p6']) )
                    else:
                        offsets[6] = int(instr['p6'])

                    if(notAComboPort(7, portCombos)):
                        s.add ( p7 == int(instr['p7']) )
                    else:
                        offsets[7] = int(instr['p7'])


                    for i in range(len(portCombos)):
                        comboVals.clear()
                        for port in portCombos[i][1:]:
                            comboVals.add(port)
                        if(gczero()):
                            left = gc(p0, "0") + gc(p1, "1") + gc(p2, "2") + gc(p3, "3") + gc(p4, "4") + gc(p5, "5") + gc(p6, "6") + gc(p7, "7")
                        else:
                            left = simplify(gc(p0, "0") + gc(p1, "1") + gc(p2, "2") + gc(p3, "3") + gc(p4, "4") + gc(p5, "5") + gc(p6, "6") + gc(p7, "7"))
                        right = gc(int(instr['p0']), "0") + gc(int(instr['p1']), "1") + gc(int(instr['p2']), "2")
                        + gc(int(instr['p3']), "3") + gc(int(instr['p4']), "4") + gc(int(instr['p5']), "p5")
                        + gc(int(instr['p6']), 'p6') + gc(int(instr['p7']), 'p7') + int(portCombos[i][0])
                        s.add ( left == right )

                        comboVals.clear()
                        offset = 0
                        for combo in portCombos:
                            if str(i) in combo[1:]:
                                offset += int(combo[0])
                                for value in combo[1:]:
                                    comboVals.add(value)
                        if(gczero()):
                            left = gc(p0, "0") + gc(p1, "1") + gc(p2, "2") + gc(p3, "3") + gc(p4, "4") + gc(p5, "5") + gc(p6, "6") + gc(p7, "7")
                        else:
                            left = simplify(gc(p0, "0") + gc(p1, "1") + gc(p2, "2") + gc(p3, "3") + gc(p4, "4") + gc(p5, "5") + gc(p6, "6") + gc(p7, "7"))
                        right = ( gc(int(instr['p0']), "0") + gc(int(instr['p1']), "1") + gc(int(instr['p2']), "2")
                        + gc(int(instr['p3']), "3") + gc(int(instr['p4']), "4") + gc(int(instr['p5']), "p5")
                        + gc(int(instr['p6']), 'p6') + gc(int(instr['p7']), 'p7') + offset )
                        s.add ( left == right )

                    if(s.check().r == Z3_L_TRUE):
                        details = instrDetails[instr['instr']]
                        line = details['name'] + ' ' + details['category'] + ' ' + details['extension'] + ' ' + details['iclass'] + ' ' + details['iform'] + ' ' + details['isa-set']
                        print("Matches instruction: " + line)
                        print s.model()
                        print s.unsat_core
                        raw_input()
                        match = True
                        #break      #don't break as it will often match multiple instructions
                    elif instr['instr'] == 'RDMSR':
                        print s.unsat_core
                    s.reset()

            if not match:
                #TODO don't forget naive heuristics - if there's no matches then just say 'ok mostly port A, must be x category' (based on port functionality)
                if(arch == 'SKL' or arch == 'SKX' or arch == 'KBL' or arch == 'CFL'):
                    #ports 0156 are integer and vector ALU, 06 are branches, 2347 are memory operations
                    port0 = ['integer ALU', 'vector FMA/MUL/ADD/ALU/SHFT', 'divide', 'branch']
                    port1 = ['integer ALU', 'fast LEA', 'slow LEA', 'vector FMA/MUL/ADD/ALU/SHFT', 'slow integer (MUL/BSR/RCL etc)']
                    port5 = ['integer ALU', 'fast LEA', 'vector SHUF/ALU', 'CVT']
                    port6 = ['integer ALU', 'integer SHFT', 'branch']
                    port2 = ['LD', 'STA']
                    port3 = ['LD', 'STA']
                    port4 = ['STD']
                    port7 = ['STA']
                elif(arch == 'HSW' or arch == 'BDW'):
                    port0 = ['integer ALU', 'SHFT', 'vector LOG', 'vector SHFT', 'divide', 'branch', 'FP MUL', 'FMA', 'STTNI']
                    port1 = ['integer ALU', 'fast LEA', 'vector FMA/MUL/ADD/ALU/SHFT', 'slow integer (MUL/BSR/RCL etc)']
                    port5 = ['integer ALU', 'fast LEA', 'vector SHUF/ALU', 'CVT']
                    port6 = ['integer ALU', 'integer SHFT', 'branch']
                    port2 = ['LD', 'STA']
                    port3 = ['LD', 'STA']
                    port4 = ['STD']
                    port7 = ['STA']
                elif(arch == 'SNB' or arch == 'IVB'):
                    print("todo")   #TODO
                elif(arch == 'NHM' or arch == 'WSM'):
                    print("todo")   #TODO
                else:
                    print("Performance analysis not supported yet for this microarchitecture.\n")

main()
