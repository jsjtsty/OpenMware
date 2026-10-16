#!/usr/bin/env python3
"""Deterministic Squirrel 2.2 pseudo-source lifter (standard library only).

Reads SCRP/ABI32 closure streams directly to preserve literals and debug locals.
Also accepts nutdis .dis files. Unknown/unstructured regions retain raw bytecode.
Output is reverse-engineering pseudo-source, not a recompilable source archive.
"""
from __future__ import annotations
import argparse
import ast
from collections import Counter
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT/'ref/work/script_x/media/script'
OPS = ('LINE LOAD LOADINT LOADFLOAT DLOAD TAILCALL CALL PREPCALL PREPCALLK GETK MOVE NEWSLOT DELETE SET GET EQ NE ARITH BITW RETURN LOADNULLS LOADROOTTABLE LOADBOOL DMOVE JMP JNZ JZ LOADFREEVAR VARGC GETVARGV NEWTABLE NEWARRAY APPENDARRAY GETPARENT COMPARITH COMPARITHL INC INCL PINC PINCL CMP EXISTS INSTANCEOF AND OR NEG NOT BWNOT CLOSURE YIELD RESUME FOREACH POSTFOREACH DELEGATE CLONE TYPEOF PUSHTRAP POPTRAP THROW CLASS NEWSLOTA').split()
# The game's VM extends Squirrel 2 with opcode 0x3d: tostring. Confirmed at
# EXE switchD_004108b3_caseD_3d -> FUN_0040eb90(source, destination).
OPS.append('TOSTRING')
IDENT = re.compile(r'^[A-Za-z_$][\w$]*$')

@dataclass
class Ins:
    op: str
    a: int
    b: int
    c: int
    d: int
    line: int = 0
    def target(self, pc): return pc + 1 + self.b
    def raw(self, pc): return f'@{pc:04d} L{self.line} {self.op} {self.a} {self.b} {self.c} {self.d}'

@dataclass
class Proto:
    name: str = 'anonymous'
    source: str = ''
    params: list = field(default_factory=list)
    literals: list = field(default_factory=list)
    outer: list = field(default_factory=list)
    locals: list = field(default_factory=list)
    defaults: list = field(default_factory=list)
    code: list[Ins] = field(default_factory=list)
    children: list[Proto] = field(default_factory=list)
    stack: int = 0
    generator: bool = False
    varargs: bool = False
    constants: list = field(default_factory=list)
    dis_lines: list = field(default_factory=list)

class Reader:
    def __init__(self, data): self.data=data; self.pos=0
    def take(self,n):
        if n<0 or self.pos+n>len(self.data): raise ValueError(f'truncated stream at {self.pos:#x}')
        s=self.data[self.pos:self.pos+n];self.pos+=n;return s
    def unpack(self,fmt): return struct.unpack('<'+fmt,self.take(struct.calcsize('<'+fmt)))
    def integer(self): return self.unpack('i')[0]
    def tag(self,t):
        actual=self.unpack('I')[0]
        if actual!=t:raise ValueError(f'bad stream tag {actual:#x} at {self.pos-4:#x}, expected {t:#x}')
    def part(self):self.tag(0x50415254)
    def obj(self):
        t=self.unpack('I')[0]&0x00ffffff
        if t==1:return None
        if t==2:return self.integer()
        if t==4:return self.unpack('f')[0]
        if t==16:return self.take(self.integer()).decode('utf-8',errors='surrogateescape')
        raise ValueError(f'unsupported literal type {t:#x}')
    def proto(self):
        self.part();p=Proto();p.source=self.obj();p.name=self.obj() or 'anonymous';self.part()
        nl,np,no,nv,nd,ndef,ni,nf=self.unpack('8i')
        if any(n<0 or n>1000000 for n in (nl,np,no,nv,nd,ndef,ni,nf)):raise ValueError('invalid section count')
        self.part();p.literals=[self.obj() for _ in range(nl)]
        self.part();p.params=[self.obj() for _ in range(np)]
        self.part()
        for _ in range(no): p.outer.append((self.integer(),self.obj(),self.obj()))
        self.part()
        for _ in range(nv): p.locals.append((self.obj(),*self.unpack('3I')))
        self.part();lines=[self.unpack('2i') for _ in range(nd)]
        self.part();p.defaults=[self.integer() for _ in range(ndef)]
        self.part()
        for _ in range(ni):
            b,op,a,c,d=self.unpack('i4B');p.code.append(Ins(OPS[op] if op<len(OPS) else f'OP_{op}',a,b,c,d))
        li=0;line=0
        for i,x in enumerate(p.code):
            while li<len(lines) and lines[li][1]<=i:line=lines[li][0];li+=1
            x.line=line
        self.part();p.children=[self.proto() for _ in range(nf)]
        p.stack=self.integer();p.generator,p.varargs=map(bool,self.unpack('2B'));return p

def read_nut(path):
    r=Reader(path.read_bytes())
    if r.data[:4]==b'SCRP':r.take(16)
    if r.take(2)!=b'\xfa\xfa':raise ValueError('not a Squirrel closure')
    r.tag(0x53514952);r.tag(1);p=r.proto();r.tag(0x5441494c)
    # SCRP's trailer is the exported constant table (usually count=0).
    if r.pos<len(r.data):
        count=r.integer()
        if count<0 or count>100000:raise ValueError('invalid exported constant count')
        for _ in range(count):
            name=r.take(r.unpack('H')[0]).rstrip(b'\0').decode('utf-8',errors='surrogateescape');value=r.obj()
            if isinstance(value,str):value=value.rstrip('\0')
            p.constants.append((name,value))
    if r.pos!=len(r.data):raise ValueError(f'{len(r.data)-r.pos} unknown trailing bytes')
    return p

def read_dis(path):
    """nutdis fallback: local info and literal-only optimized operands may be absent."""
    stack=[];root=None
    for line in path.read_text(encoding='utf-8',errors='replace').splitlines():
        m=re.match(r'^(\s*)function (.*?)  params=\d+(.*?)  stack=(\d+)(.*)$',line)
        if m:
            depth=len(m[1])//2;p=Proto(name=m[2],params=re.findall(r'"([^"\n]*)"',m[3]),stack=int(m[4]),varargs='varargs' in m[5],generator='generator' in m[5])
            while len(stack)>depth:stack.pop()
            if stack:stack[-1].children.append(p)
            else:root=p
            stack.append(p);continue
        if stack:stack[-1].dis_lines.append(line)
        m=re.match(r'\s*(\d+) L(\d+)\s+(\S+)\s+a0=(\d+)\s+a1=(-?\d+)\s+a2=(\d+)\s+a3=(\d+)(?:\s*;\s*(.*))?',line)
        if not m:continue
        p=stack[-1];_,ln,op,a,b,c,d,comment=m.groups();x=Ins(op,int(a),int(b),int(c),int(d),int(ln));p.code.append(x)
        if int(m[1])!=len(p.code)-1:raise ValueError('noncontiguous nutdis instruction indices')
        if comment and op in ('LOAD','GETK','PREPCALLK','DLOAD'):
            try:values=ast.literal_eval('['+comment+']')
            except (ValueError,SyntaxError):values=[comment]
            for idx,val in zip([x.b,x.d] if op=='DLOAD' else [x.b],values):
                while len(p.literals)<=idx:p.literals.append(MissingLiteral(len(p.literals)))
                p.literals[idx]=val
    if root is None:raise ValueError('no nutdis functions')
    return root

@dataclass
class MissingLiteral:
    index:int

def literal(value):
    if isinstance(value,MissingLiteral):raise ValueError(f'literal {value.index} unavailable in .dis')
    if value is None:return 'null'
    if isinstance(value,str):return json.dumps(value,ensure_ascii=False)
    if isinstance(value,bool):return str(value).lower()
    return repr(value)

def member(obj,key):
    try:name=json.loads(key)
    except (ValueError,TypeError):name=None
    if isinstance(name,str) and IDENT.fullmatch(name):return ('::'+name if obj=='::' else obj+'.'+name)
    return obj+'['+key+']'

def signed8(v):return v-256 if v>127 else v

def registers(x,p):
    """Exact VM register uses/definitions, for liveness at control-flow joins."""
    a,b,c,d=x.a,x.b,x.c,x.d;op=x.op;r=set();w={a}
    if op in ('LOAD','LOADINT','LOADFLOAT','LOADBOOL','LOADROOTTABLE','NEWTABLE','NEWARRAY','VARGC','LOADFREEVAR'):pass
    elif op=='DLOAD':w.add(c)
    elif op=='LOADNULLS':w=set(range(a,a+b))
    elif op in ('MOVE','NEG','NOT','BWNOT','TYPEOF','CLONE','RESUME','TOSTRING','GETPARENT','GETVARGV'):r={b}
    elif op=='DMOVE':r={b,d};w.add(c)
    elif op in ('GETK','PREPCALLK'):r={c};w|={d} if op=='PREPCALLK' else set()
    elif op=='PREPCALL':r={b,c};w.add(d)
    elif op in ('GET','EXISTS','INSTANCEOF','DELEGATE','DELETE','ARITH','BITW','CMP'):r={b,c}
    elif op in ('EQ','NE'):r={c}|({b} if not d else set())
    elif op in ('CALL','TAILCALL'):r={b}|set(range(c,c+d));w=set() if op=='TAILCALL' else {a}
    elif op in ('SET','NEWSLOT','NEWSLOTA'):r={b,c,d};w=set() if op=='NEWSLOTA' else {a}
    elif op=='APPENDARRAY':r={a}|({b} if not d else set());w=set()
    elif op in ('RETURN','YIELD'):r={b} if a!=255 else set();w=set()
    elif op=='THROW':r={a};w=set()
    elif op in ('JZ','JNZ'):r={a};w=set()
    elif op in ('AND','OR'):r={c};w=set() # destination is conditional
    elif op in ('FOREACH','POSTFOREACH'):r={a,c,c+1,c+2};w=set()
    elif op in ('INC','PINC'):r={b,c}
    elif op in ('INCL','PINCL'):r={b};w.add(b)
    elif op=='COMPARITH':r={(b>>16)&65535,b&65535,c}
    elif op=='COMPARITHL':r={b,c};w.add(b)
    elif op=='CLASS':r=({b} if b!=-1 else set())|({c} if c!=255 else set())
    elif op=='CLOSURE':
        if 0<=b<len(p.children):r={src for kind,src,_ in p.children[b].outer if kind==0}|set(p.children[b].defaults)
    elif op in ('LINE','JMP','PUSHTRAP','POPTRAP'):w=set()
    return {i for i in r if isinstance(i,int) and 0<=i<255},{i for i in w if 0<=i<255}

def liveness(p):
    n=len(p.code);live=[set() for _ in range(n+1)];rw=[registers(x,p) for x in p.code]
    successors=[]
    for i,x in enumerate(p.code):
        t=x.target(i)
        if x.op=='JMP':s=[t]
        elif x.op in ('RETURN','TAILCALL','THROW'):s=[]
        elif x.op in ('JZ','JNZ','AND','OR','FOREACH','POSTFOREACH','PUSHTRAP'):s=[i+1,t]
        else:s=[i+1]
        successors.append([j for j in s if 0<=j<=n])
    changed=True
    while changed:
        changed=False
        for i in range(n-1,-1,-1):
            reads,writes=rw[i];out=set().union(*(live[j] for j in successors[i]));value=reads|(out-writes)
            if value!=live[i]:live[i]=value;changed=True
    return live

def materialized_definitions(p,live):
    # Only the last definition reaching another basic block needs a named
    # temporary. Calls also end blocks to retain argument evaluation order.
    boundaries={0,len(p.code)}
    for i,x in enumerate(p.code):
        if x.op in ('JMP','JZ','JNZ','AND','OR','FOREACH','POSTFOREACH','PUSHTRAP','RETURN','TAILCALL','THROW','CALL','RESUME','TOSTRING'):
            boundaries.add(i+1)
            if x.op not in ('RETURN','TAILCALL','THROW','CALL','RESUME','TOSTRING') and 0<=x.target(i)<=len(p.code):boundaries.add(x.target(i))
    result={};points=sorted(boundaries)
    for lo,hi in zip(points,points[1:]):
        last={}
        for i in range(lo,hi):
            for r in registers(p.code[i],p)[1]:last[r]=i
        x=p.code[hi-1]
        targets=[hi]
        if x.op=='JMP':targets=[x.target(hi-1)]
        elif x.op in ('JZ','JNZ','AND','OR','FOREACH','POSTFOREACH','PUSHTRAP'):targets.append(x.target(hi-1))
        elif x.op in ('RETURN','TAILCALL','THROW'):targets=[]
        outgoing=set().union(*(live[t] for t in targets if 0<=t<=len(p.code)))
        for r in outgoing:
            if r in last:result.setdefault(last[r],set()).add(r)
    return result

class Unstructured(Exception):pass

class Lifter:
    def __init__(self,p,stats=None):
        self.p=p;self.code=p.code;self.stats=stats if stats is not None else Counter();self.reg={i:str(n) for i,n in enumerate(p.params)}
        self.out=[];self.indent=0;self.line=-1;self.pc=0;self.declared=set(map(str,p.params));self.prepared={};self.used_children=set();self.last_assign={};self.known={}
        self.localnames={}
        used=Counter()
        for name,pos,start,end in p.locals:
            if not isinstance(name,str) or not IDENT.fullmatch(name):name=f'v{pos}'
            key=(pos,start,end);used[name]+=1
            self.localnames[key]=name if used[name]==1 else f'{name}_{used[name]}'
        self.loop_headers={}
        for pc,x in enumerate(self.code):
            if x.op in ('JMP','JNZ','JZ') and x.target(pc)<=pc:
                self.loop_headers.setdefault(x.target(pc),[]).append(pc)
        self.hoisted=set()
        self.live=liveness(p)
        self.materialized_at=materialized_definitions(p,self.live)
        self.materialized=set().union(*self.materialized_at.values()) if self.materialized_at else set()
    def emit(self,text,line=None):
        ln=self.code[self.pc].line if line is None and self.pc<len(self.code) else line
        suffix=f' // L{ln}' if ln and ln!=self.line else ''
        if ln:self.line=ln
        parts=text.splitlines() or ['']
        self.out.extend('    '*self.indent+s+(suffix if i==0 else '') for i,s in enumerate(parts))
    def local(self,r,pc=None):
        pc=self.pc if pc is None else pc
        if r<len(self.p.params):return str(self.p.params[r])
        found=[(start,end,n) for (pos,start,end),n in self.localnames.items() if pos==r and start<=pc<=end]
        if found:return max(found)[2]
        # Local declarations are recorded with start_op one instruction after
        # initialization; recognize the instruction immediately before it.
        found=[(start,end,n) for (pos,start,end),n in self.localnames.items() if pos==r and start==pc+1]
        return min(found)[2] if found else None
    def get(self,r):return self.known.get(r,self.reg.get(r,self.local(r) or f'v{r}'))
    def set(self,r,value,force=False):
        if r==255:return
        self.prepared.pop(r,None)
        self.known.pop(r,None)
        name=self.local(r)
        is_local=name is not None
        if name or force or r in self.materialized and r in self.materialized_at.get(self.pc,()):
            name=name or f'v{r}';prefix=''
            if value!=name:self.emit(f'{prefix}{name} = {value};')
            self.declared.add(name);self.reg[r]=name
        else:self.reg[r]=value
        if not is_local and isinstance(value,str) and re.fullmatch(r'this|::|null|true|false|[-+]?\d+(?:\.\d+)?(?:e[-+]?\d+)?|"(?:\\.|[^"\\])*"',value):self.known[r]=value
    def lit(self,i):
        if not 0<=i<len(self.p.literals):raise Unstructured(f'literal {i} unavailable')
        try:return literal(self.p.literals[i])
        except ValueError as e:raise Unstructured(str(e))
    def freeze(self):
        # Materialize temporary expressions at control-flow boundaries; names
        # and constants alone need no evaluation. This preserves evaluation
        # time of field reads and avoids leaking branch-local substitutions.
        for r,v in list(self.reg.items()):
            if r not in self.live[min(self.pc,len(self.code))]:continue
            if isinstance(v,str) and (v.startswith(('function','class ')) or v in ('::','this') or re.fullmatch(r'[\w$]+|[-+]?\d+(?:\.\d+)?|"(?:\\.|[^"\\])*"',v)):continue
            if isinstance(v,str):self.set(r,v,True)
    def reset(self,base=None):
        self.known.clear()
        self.prepared.clear()
        if base is not None:self.reg=dict(base)
        for r in range(self.p.stack):
            if self.local(r):self.reg[r]=self.local(r)
    def flush_join(self,join,base):
        for r in sorted(self.live[join]):
            value=self.get(r)
            if self.reg.get(r)!=base.get(r) and not self.local(r):self.set(r,value,True)
    def expression_region(self,start,end):
        pure={'LOAD','LOADINT','LOADFLOAT','LOADBOOL','LOADNULLS','LOADROOTTABLE','DLOAD','MOVE','DMOVE','GETK','GET','EQ','NE','ARITH','BITW','CMP','NOT','NEG','BWNOT','TYPEOF','EXISTS','INSTANCEOF','LOADFREEVAR','VARGC','GETVARGV'}
        if any(x.op not in pure for x in self.code[start:end]):return None
        reg,known,decl,mat,pc,line,prepared=dict(self.reg),dict(self.known),set(self.declared),self.materialized,self.pc,self.line,dict(self.prepared)
        length=len(self.out);self.materialized=set()
        try:
            for i in range(start,end):self.pc=i;self.instruction(self.code[i])
            result={r:self.get(r) for r in self.reg}
            return result if len(self.out)==length else None
        finally:
            del self.out[length:];self.reg=reg;self.known=known;self.declared=decl;self.materialized=mat;self.pc=pc;self.line=line;self.prepared=prepared
    def child(self,index):
        if not 0<=index<len(self.p.children):raise Unstructured(f'child {index} missing')
        self.used_children.add(index)
        child=self.p.children[index]
        captures=[]
        for kind,src,name in child.outer:
            value=self.get(src) if kind==0 else (str(self.p.outer[src][2]) if kind==2 and src<len(self.p.outer) else member('this',literal(src)))
            captures.append(str(name)+' = '+value)
        return Lifter(child,self.stats).render(expression=True,defaults=[self.get(r) for r in child.defaults],captures=captures)
    def instruction(self,x):
        a,b,c,d=x.a,x.b,x.c,x.d;g=self.get;s=self.set;op=x.op
        if op=='LINE':return
        if op=='LOAD':s(a,self.lit(b))
        elif op=='LOADINT':s(a,str(b))
        elif op=='LOADFLOAT':s(a,repr(struct.unpack('<f',struct.pack('<i',b))[0]))
        elif op=='DLOAD':s(a,self.lit(b));s(c,self.lit(d))
        elif op=='LOADBOOL':s(a,'true' if b else 'false')
        elif op=='LOADNULLS':
            for r in range(a,a+b):s(r,'null')
        elif op=='LOADROOTTABLE':s(a,'::')
        elif op=='MOVE':s(a,g(b))
        elif op=='DMOVE':
            v,w=g(b),g(d);s(a,v);s(c,w)
        elif op=='GETK':s(a,member(g(c),self.lit(b)))
        elif op=='GET':s(a,member(g(b),g(c)))
        elif op in ('PREPCALLK','PREPCALL'):
            obj=g(c);key=self.lit(b) if op=='PREPCALLK' else g(b)
            fn=member(obj,key);s(d,obj);s(a,fn);self.prepared[a]=(fn,obj)
            self.known.pop(a,None)
        elif op in ('CALL','TAILCALL'):
            fn=g(b);args=', '.join(g(r) for r in range(c+1,c+d))
            prepared=self.prepared.get(b)
            if prepared and fn!=prepared[0]:value=fn+'.call('+g(c)+(', '+args if args else '')+')'
            else:value=fn+'('+args+')'
            if op=='TAILCALL':self.emit('return '+value+';')
            elif a==255:self.emit(value+';')
            else:s(a,value,True)
        elif op in ('SET','NEWSLOT','NEWSLOTA'):
            dest=member(g(b),g(c));value=g(d)
            self.emit(('static ' if op=='NEWSLOTA' and a&2 else '')+dest+(' <- ' if op!='SET' else ' = ')+value+';')
            if op=='NEWSLOTA' and a&1:self.emit('attributes('+dest+', '+g(c-1)+');')
            if op!='NEWSLOTA' and a!=d:s(a,value)
        elif op in ('NEWTABLE','NEWARRAY'):s(a,'{}' if op=='NEWTABLE' else '[]',True)
        elif op=='APPENDARRAY':self.emit(g(a)+'.append('+(self.lit(b) if d else g(b))+');')
        elif op in ('EQ','NE'):
            right=self.lit(b) if d else g(b);s(a,'('+g(c)+(' == ' if op=='EQ' else ' != ')+right+')')
        elif op in ('ARITH','BITW','CMP'):
            operator=chr(d) if op=='ARITH' else ({0:'&',2:'|',3:'^',4:'<<',5:'>>',6:'>>>'} if op=='BITW' else {0:'>',2:'>=',3:'<',4:'<='}).get(d)
            if operator is None:raise Unstructured('unknown arithmetic operator')
            s(a,'('+g(c)+' '+operator+' '+g(b)+')')
        elif op in ('NEG','NOT','BWNOT','TYPEOF','CLONE','RESUME'):
            val={'NEG':'-','NOT':'!','BWNOT':'~','TYPEOF':'typeof ','CLONE':'clone ','RESUME':'resume '}[op]
            s(a,'('+val+g(b)+')',op=='RESUME')
        elif op=='EXISTS':s(a,'('+g(c)+' in '+g(b)+')')
        elif op=='INSTANCEOF':s(a,'('+g(c)+' instanceof '+g(b)+')')
        elif op=='DELETE':s(a,'delete '+member(g(b),g(c)),True)
        elif op=='RETURN':self.emit('return'+(' '+g(b) if a!=255 else '')+';')
        elif op=='YIELD':self.emit('yield'+(' '+g(b) if a!=255 else '')+';')
        elif op=='THROW':self.emit('throw '+g(a)+';')
        elif op=='LOADFREEVAR':s(a,str(self.p.outer[b][2]) if b<len(self.p.outer) else f'freevar{b}')
        elif op=='VARGC':s(a,'vargv.len()')
        elif op=='GETVARGV':s(a,'vargv['+g(b)+']')
        elif op=='GETPARENT':s(a,'getparent('+g(b)+')')
        elif op=='TOSTRING':s(a,'('+g(b)+').tostring()',True)
        elif op=='DELEGATE':s(a,'delegate('+g(b)+', '+g(c)+')',True)
        elif op in ('COMPARITH','COMPARITHL'):
            dest=member(g((b>>16)&65535),g(c)) if op=='COMPARITH' else (self.local(b) or self.reg.get(b,f'v{b}'))
            rhs=g(b&65535) if op=='COMPARITH' else g(c)
            value='('+dest+' '+chr(d)+'= '+rhs+')';s(a,value,True)
        elif op in ('INC','INCL','PINC','PINCL'):
            dest=member(g(b),g(c)) if op in ('INC','PINC') else (self.local(b) or self.reg.get(b,f'v{b}'));sign='++' if signed8(d)>0 else '--'
            value='('+dest+sign+')' if op in ('PINC','PINCL') else '('+sign+dest+')';s(a,value,True)
        elif op=='CLOSURE':s(a,self.child(b))
        elif op=='CLASS':s(a,'class'+(' extends '+g(b) if b!=-1 else '')+' {}',True)
        elif op=='POPTRAP':self.emit(f'// pop {a} exception trap(s)')
        else:raise Unstructured('unhandled '+op)

    def switch_shape(self,i,end):
        """Recognize SQCompiler::SwitchStatement's skip-next-test edges."""
        if i+1>=end or self.code[i].op!='EQ' or self.code[i+1].op!='JZ':return None
        first=self.code[i];cases=[];eq=i;jz=i+1;entry=i
        while True:
            test=self.code[jz];nxt=test.target(jz)
            if not jz<nxt<=end or self.code[eq].c!=first.c:return None
            skip=nxt-1
            # A skip-test edge jumps over the next case comparison when
            # falling through from a preceding case's body.
            if self.code[skip].op=='JMP' and nxt<self.code[skip].target(skip)<=end:
                body2=self.code[skip].target(skip);jz2=body2-1;eq2=jz2-1
                if eq2>=nxt and self.code[eq2].op=='EQ' and self.code[jz2].op=='JZ' and self.code[eq2].c==first.c:
                    if any(x.op not in ('LOAD','LOADINT','LOADFLOAT','LOADBOOL','LOADNULLS','MOVE','GETK','GET','NEG') for x in self.code[nxt:eq2]):return None
                    cases.append((entry,eq,jz,jz+1,skip));entry=nxt;eq=eq2;jz=jz2;continue
            cases.append((entry,eq,jz,jz+1,nxt))
            if len(cases)<2:return None
            # Breaks can skip a default body. The common compiler target is
            # the farthest forward exit referenced from a case body.
            exits=[x.target(pc) for _,_,_,lo,hi in cases for pc in range(lo,hi) for x in [self.code[pc]] if x.op=='JMP' and nxt<=x.target(pc)<=end]
            join=max([nxt]+exits)
            return first.c,cases,nxt,join

    def emit_switch(self,shape,loop,active):
        value,cases,default,join=shape;self.freeze();base=dict(self.reg);selector=self.get(value)
        # Recover case expressions without moving emitted operations across
        # case tests. Shape recognition excludes calls and mutations here.
        keys=[];savedout=len(self.out);savedregs=dict(self.reg);savedknown=dict(self.known);saveddecl=set(self.declared)
        for entry,eq,jz,lo,hi in cases:
            if entry!=eq:
                for pc in range(entry,eq):self.pc=pc;self.instruction(self.code[pc])
            x=self.code[eq];keys.append(self.lit(x.b) if x.d else self.get(x.b))
        if len(self.out)!=savedout:
            self.out=self.out[:savedout];self.reg=savedregs;self.known=savedknown;self.declared=saveddecl;return False
        self.reg=savedregs;self.known=savedknown;self.pc=cases[0][1];self.emit('switch ('+selector+') {');self.indent+=1
        for key,(_,eq,jz,lo,hi) in zip(keys,cases):
            self.reset(base);self.pc=eq;self.emit('case '+key+':');self.indent+=1
            self.region(lo,hi,(-1,join),active);self.indent-=1
        if default<join:
            self.reset(base);self.pc=default;self.emit('default:');self.indent+=1;self.region(default,join,(-1,join),active);self.indent-=1
        self.indent-=1;self.pc=join-1;self.emit('}');self.reset(base);return True

    def region(self,start,end,loop=None,active=frozenset()):
        i=start
        while i<end:
            self.pc=i;x=self.code[i];op=x.op;t=x.target(i)
            shape=self.switch_shape(i,end)
            if shape and self.emit_switch(shape,loop,active):i=shape[3];continue
            # Natural backedges delimit while/for/do loops. The condition's
            # computation stays inside the loop, so calls are reevaluated.
            tails=[] if any(h==i for h,_ in active) else [j for j in self.loop_headers.get(i,[]) if j<end]
            if tails and op!='FOREACH':
                tail=max(tails);back=self.code[tail];exit=tail+1;self.freeze();base=dict(self.reg)
                self.known.clear()
                increment=None;bodytail=tail
                if back.op=='JMP' and tail>i:
                    last=self.code[tail-1]
                    skip_increment=any(y.op in ('JMP','JZ','JNZ') and y.target(pc)==tail for pc,y in enumerate(self.code[i:tail-1],i))
                    if last.op in ('PINCL','INCL') and self.local(last.b,tail-1) and not skip_increment and last.a not in self.live[tail]:
                        increment=self.local(last.b,tail-1)+('++' if signed8(last.d)>0 else '--');bodytail=tail-1
                self.emit('do {' if back.op in ('JZ','JNZ') else ('for (; true; '+increment+') {' if increment else 'while (true) {'));self.indent+=1
                self.region(i,bodytail,(i,exit,bodytail) if increment else (i,exit),active|{(i,tail)})
                self.pc=tail
                cond=self.get(back.a)
                self.indent-=1
                self.emit('} while ('+('!('+cond+')' if back.op=='JZ' else cond)+');' if back.op in ('JZ','JNZ') else '}')
                self.reset(base);i=exit;continue
            if op in ('AND','OR') and i<t<=end:
                expression=self.expression_region(i+1,t)
                if expression is not None and x.a in expression:
                    self.set(x.a,'('+self.get(x.c)+(' && ' if op=='AND' else ' || ')+expression[x.a]+')');i=t;continue
                left=self.get(x.c);before=dict(self.reg);known=dict(self.known);outlen=len(self.out);oldindent=self.indent
                self.region(i+1,t,loop,active)
                right=self.get(x.a);right_lines=self.out[outlen:];del self.out[outlen:];self.reg=before;self.known=known;self.pc=i
                if not right_lines:self.set(x.a,'('+left+(' && ' if op=='AND' else ' || ')+right+')')
                else:
                    self.set(x.a,left,True);self.emit('if ('+(''+self.get(x.a) if op=='AND' else '!('+self.get(x.a)+')')+') {');self.indent=oldindent+1
                    self.out.extend('    '+line for line in right_lines);self.set(x.a,right,True);self.indent=oldindent;self.emit('}')
                    self.known.clear();self.reg[x.a]=self.local(x.a) or f'v{x.a}'
                i=t;continue
            if op=='FOREACH' and i<t<=end:
                tail=t-1
                if self.code[tail].op!='JMP' or self.code[tail].target(tail)!=i:raise Unstructured('foreach backedge mismatch')
                container=self.get(x.a);self.freeze();base=dict(self.reg);self.known.clear();self.pc=i+2
                k=self.local(x.c) or f'v{x.c}';v=self.local(x.c+1) or f'v{x.c+1}'
                self.reg[x.c]=k;self.reg[x.c+1]=v;self.declared.update((k,v))
                self.pc=i;self.emit(f'foreach ({k}, {v} in {container}) {{');self.indent+=1
                begin=i+2 if i+1<len(self.code) and self.code[i+1].op=='POSTFOREACH' else i+1
                self.region(begin,tail,(i,t),active|{(i,tail)});self.indent-=1;self.pc=tail;self.emit('}');self.reset(base);i=t;continue
            if op=='PUSHTRAP' and i<t<=end:
                jump=t-1
                if self.code[jump].op!='JMP':raise Unstructured('trap join missing')
                join=self.code[jump].target(jump)
                if not t<=join<=end:raise Unstructured('trap join outside region')
                self.freeze();base=dict(self.reg);self.emit('try {');self.indent+=1
                bodyend=jump-1 if jump>i+1 and self.code[jump-1].op=='POPTRAP' else jump
                self.region(i+1,bodyend,loop,active);self.indent-=1;self.reset(base);self.pc=t
                name=self.local(x.a) or f'v{x.a}';self.reg[x.a]=name;self.declared.add(name)
                self.emit('} catch ('+name+') {');self.indent+=1;self.region(t,join,loop,active);self.indent-=1;self.emit('}');self.reset(base);i=join;continue
            if op in ('JZ','JNZ'):
                cond=self.get(x.a);cond=cond if op=='JZ' else '!('+cond+')'
                if loop and t==loop[1]:
                    self.emit('if ('+('!('+self.get(x.a)+')' if op=='JZ' else self.get(x.a))+') break;');i+=1;continue
                if loop and t==loop[0]:
                    self.emit('if ('+('!('+self.get(x.a)+')' if op=='JZ' else self.get(x.a))+') continue;');i+=1;continue
                if loop and len(loop)>2 and t==loop[2]:
                    self.emit('if ('+('!('+self.get(x.a)+')' if op=='JZ' else self.get(x.a))+') continue;');i+=1;continue
                if not i<t<=end:raise Unstructured(f'conditional edge {i}->{t} crosses region {start}:{end}')
                jump=t-1;join=None
                if jump>i and self.code[jump].op=='JMP' and t<self.code[jump].target(jump)<=end:join=self.code[jump].target(jump)
                if join:
                    left_expr=self.expression_region(i+1,jump);right_expr=self.expression_region(t,join)
                    if left_expr is not None and right_expr is not None:
                        changed=[r for r in self.live[join] if left_expr.get(r)!=right_expr.get(r)]
                        if len(changed)==1:
                            r=changed[0]
                            if r in left_expr and r in right_expr and not self.local(r):
                                self.set(r,'('+cond+' ? '+left_expr[r]+' : '+right_expr[r]+')');i=join;continue
                self.freeze();base=dict(self.reg);cond=self.get(x.a) if op=='JZ' else '!('+self.get(x.a)+')'
                self.emit('if ('+cond+') {');self.indent+=1
                self.region(i+1,jump if join else t,loop,active);left=dict(self.reg);self.indent-=1
                if join:
                    self.reset(base);self.pc=t;self.emit('} else {');self.indent+=1;self.region(t,join,loop,active);right=dict(self.reg);self.indent-=1;i=join
                else:right=base;i=t
                self.emit('}');self.reg={r:v for r,v in left.items() if right.get(r)==v};self.reset();continue
            if op=='JMP':
                if t==i+1:i+=1;continue
                if loop and t==loop[0]:self.emit('continue;');i+=1;continue
                if loop and len(loop)>2 and t==loop[2]:self.emit('continue;');i+=1;continue
                if loop and t==loop[1]:self.emit('break;');i+=1;continue
                if t==end:self.emit('break;' if loop else '// end of region');i=end;continue
                raise Unstructured(f'jump {i}->{t} crosses structured region')
            if op=='POSTFOREACH':raise Unstructured('orphan generator postforeach')
            self.instruction(x);i+=1

    def render(self,expression=False,defaults=None,captures=None):
        snapshot=self.stats.copy()
        self.stats['functions']+=1
        parameters=list(map(str,self.p.params[1:]))
        if defaults:
            for i,value in enumerate(defaults):parameters[len(parameters)-len(defaults)+i]+=' = '+value
        params=', '.join(parameters);params+=(', ' if params else '')+'...' if self.p.varargs else ''
        header='function'+(' '+self.p.name if not expression and IDENT.fullmatch(self.p.name) else '')+'('+params+') {'
        if captures:header+=' /* captured values: '+', '.join(captures).replace('*/','* /')+' */'
        self.emit(header,line=0);self.indent=1
        try:self.region(0,len(self.code))
        except (Unstructured,RecursionError,ValueError,IndexError) as error:
            # Whole-function fallback is intentionally conservative: retaining
            # a misleading partial lift is worse than preserving all opcodes.
            self.stats.clear();self.stats.update(snapshot);self.stats['functions']+=1;self.stats['fallback']+=1;self.out=self.out[:1];self.indent=1
            self.emit('// FALLBACK: '+str(error),line=0)
            self.emit('// literals: '+json.dumps([repr(x) for x in self.p.literals],ensure_ascii=True),line=0)
            self.emit('// locals/outer/defaults: '+json.dumps([self.p.locals,self.p.outer,self.p.defaults],ensure_ascii=True),line=0)
            for pc,x in enumerate(self.code):self.emit('// '+x.raw(pc),line=x.line)
            if self.p.dis_lines:
                self.emit('// Original nutdis text (including literal continuations):',line=0)
                for line in self.p.dis_lines:self.emit('// '+line,line=0)
            self.used_children.clear()
        for idx,child in enumerate(self.p.children):
            if idx not in self.used_children:
                self.emit('// nested prototype '+str(idx)+'; outer/default register metadata: '+json.dumps([child.outer,child.defaults],ensure_ascii=True),line=0)
                self.emit(Lifter(child,self.stats).render(),line=0)
        self.indent=0;self.emit('}',line=0)
        declarations=sorted(self.declared-set(map(str,self.p.params)))
        if declarations and not any('// FALLBACK:' in s for s in self.out[1:2]):self.out.insert(1,'    local '+', '.join(declarations)+';')
        return '\n'.join(self.out)

def pretty_aggregates(text):
    """Fold adjacent object construction stores, preserving evaluation order."""
    lines=text.splitlines();out=[];i=0
    while i<len(lines):
        m=re.match(r'^(\s*)(v\d+) = (class(?: extends .*?)? \{\}|\{\}|\[\]);(.*)$',lines[i])
        if not m:out.append(lines[i]);i+=1;continue
        indent,var,kind,comment=m.groups();j=i+1;members=[]
        while j<len(lines):
            if kind=='[]':
                n=re.match(re.escape(indent+var)+r'\.append\((.*)\);(.*)$',lines[j])
                if not n:break
                members.append([indent+'    '+n[1]+','+n[2]]);j+=1
            else:
                n=re.match(re.escape(indent)+r'(static )?'+re.escape(var)+r'(\.\w+|\[.*?\]) <- (.*)$',lines[j])
                if not n:break
                key=n[2][1:] if n[2].startswith('.') else n[2];entry=[indent+'    '+(n[1] or '')+key+' = '+n[3]];j+=1
                if not re.search(r';(?: //.*)?$',n[3]):
                    while j<len(lines):
                        entry.append('    '+lines[j]);closed=lines[j].startswith(indent+'};');j+=1
                        if closed:break
                if kind=='{}':entry[-1]=re.sub(r';(?=\s*(?://.*)?$)',',',entry[-1])
                members.append(entry)
        if not members:out.append(lines[i]);i+=1;continue
        opener='[' if kind=='[]' else (kind[:-1] if kind.startswith('class') else '{')
        out.append(indent+var+' = '+opener+comment)
        for entry in members:out.extend(entry)
        out.append(indent+('];' if kind=='[]' else '};'));i=j
    return '\n'.join(out)

def decompile(path):
    p=read_dis(path) if path.suffix=='.dis' else read_nut(path)
    stats=Counter();body=Lifter(p,stats).render();constants=''.join('const '+name+' = '+literal(value)+';\n' for name,value in p.constants)
    return '// Pseudo-Squirrel; generated by scripts/nutdec.py\n'+constants+pretty_aggregates(body)+'\n',stats

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file',type=Path,nargs='?');parser.add_argument('--all',dest='out',nargs='?',const=str(ROOT/'ref/work/dec'),type=Path)
    parser.add_argument('--scripts-root',type=Path,default=SCRIPTS)
    args=parser.parse_args()
    if hasattr(sys.stdout,'reconfigure'):sys.stdout.reconfigure(encoding='utf-8',errors='backslashreplace',newline='\n')
    if args.out is not None:
        start=time.perf_counter();stats=Counter();failures=[];fallback_files=[]
        for path in sorted(args.scripts_root.rglob('*.nut')):
            dest=args.out/path.relative_to(args.scripts_root).with_suffix('.nut.txt');dest.parent.mkdir(parents=True,exist_ok=True)
            try:
                body,counts=decompile(path);dest.write_text(body,encoding='utf-8',errors='backslashreplace',newline='\n');stats.update(counts);stats['files_ok']+=1
                if counts['fallback']:fallback_files.append(str(path.relative_to(args.scripts_root)))
            except Exception as error:failures.append({'file':str(path),'error':str(error)})
        summary=dict(stats);summary['fallback']=stats['fallback'];summary['clean_functions']=stats['functions']-stats['fallback'];summary['fallback_percent']=round(100*stats['fallback']/max(1,stats['functions']),4);summary['seconds']=round(time.perf_counter()-start,2);summary['failures']=failures;summary['fallback_files']=fallback_files
        args.out.mkdir(parents=True,exist_ok=True);(args.out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:v for k,v in summary.items() if k!='fallback_files'},ensure_ascii=False));return bool(failures)
    if args.file is None:parser.error('provide FILE.nut, FILE.dis or --all OUTDIR')
    body,_=decompile(args.file);sys.stdout.write(body);return 0

if __name__=='__main__':raise SystemExit(main())
