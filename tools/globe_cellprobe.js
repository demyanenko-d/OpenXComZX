// Оценка перекладки геометрии глобуса по мелким ячейкам (globe.md §12.9): рёбра ресурса GLOBE
// (tmp/sd/OXZ/<игра>/GEO.PAK) по сеткам 30/15/10/5° (по середине ребра), вершин и рёбер к проекции
// после отсева ячеек как в движке на случайных видах, выбор кандидатов таблицей «зум x полоса
// широты вида x ряд ячеек» (сетка 15°) и ширина T_hi по рамкам кандидатов.
//   node tools/globe_cellprobe.js; GAME=UFO node tools/globe_cellprobe.js
// Оценка: вершин/рёбер к проекции после отсева ячеек при сетках 30/15/10° (зумы 3–5)
const fs=require('fs');
const game=process.env.GAME||'TFTD';
const pak=fs.readFileSync(`tmp/sd/OXZ/${game}/GEO.PAK`);
function res(id){const n=pak.readUInt16LE(6);for(let i=0;i<n;i++){const e=16+i*16;if(pak.readUInt16LE(e)===id){const s=pak.readUInt16LE(e+4),z=pak.readUInt32LE(e+6);return pak.subarray(s*512,s*512+z);}}}
const G=res(0x0145);
const nCell=G.readUInt16LE(0);
const ct=6, bt=ct+nCell*16;
const rdv=o=>{const r=[];for(let k=0;k<3;k++){const lo=G[o+k*2],hi=G.readInt8(o+k*2+1);r.push((hi*128+lo)/16384);}return r;};
const edges=[];
for(let c=0;c<nCell;c++){const r=ct+c*16, off=G.readUInt16LE(r), vn=G.readUInt16LE(r+2), en=G.readUInt16LE(r+4);
 const vb=bt+off, V=[];for(let i=0;i<vn;i++)V.push(rdv(vb+i*6));
 for(let i=0;i<en;i++){const eo=vb+vn*6+i*6;edges.push([V[G.readUInt16LE(eo)/6],V[G.readUInt16LE(eo+2)/6]]);}}
const ang=(a,b)=>Math.acos(Math.max(-1,Math.min(1,a[0]*b[0]+a[1]*b[1]+a[2]*b[2])));
const L=edges.map(([a,b])=>ang(a,b)*180/Math.PI).sort((x,y)=>x-y);
console.log(game,'edges',edges.length,'length deg: median',L[L.length>>1].toFixed(2),'p90',L[L.length*0.9|0].toFixed(2),'max',L[L.length-1].toFixed(2));
function grid(deg){const nl=360/deg, nb=180/deg, cells=[];for(let i=0;i<nl*nb;i++)cells.push({e:[],v:new Set()});
 for(const e of edges){const m=[e[0][0]+e[1][0],e[0][1]+e[1][1],e[0][2]+e[1][2]];const n=Math.hypot(...m);
  const lon=Math.atan2(m[1],m[0])*180/Math.PI, lat=Math.asin(m[2]/n)*180/Math.PI;
  const cl=Math.floor(((lon%360)+360)%360/deg)%nl, cb=Math.min(nb-1,Math.max(0,Math.floor((lat+90)/deg)));
  const c=cells[cb*nl+cl]; c.e.push(e); c.v.add(e[0].join()); c.v.add(e[1].join());}
 for(const c of cells){if(!c.e.length)continue;let s=[0,0,0];for(const e of c.e)for(const q of e)for(let k=0;k<3;k++)s[k]+=q[k];const n=Math.hypot(...s);c.cc=s.map(x=>x/n);
  let r=0;for(const e of c.e)for(const q of e)r=Math.max(r,ang(c.cc,q));r+=0.5*Math.PI/180;c.sr=r>=Math.PI/2?1:Math.sin(r);c.row=Math.floor(cells.indexOf(c)/nl);c.col=cells.indexOf(c)%nl;}
 return {deg,nl,nb,cells};}
const R=[90,120,180,280,450,720];
function view(g,lon0,lat0,z){const cl=Math.cos(lon0),sl=Math.sin(lon0),cC=Math.cos(lat0),sC=Math.sin(lat0),r=R[z];
 let nc=0,vis=0,nv=0,ne=0,win=0;
 const P=q=>{const W=q[0]*cl+q[1]*sl;return [128+r*(q[1]*cl-q[0]*sl),100+r*(cC*q[2]-sC*W),cC*W+sC*q[2]];};
 for(const c of g.cells){if(!c.e.length)continue;nc++;
  const p=P(c.cc);const m=r*c.sr*1.125+2;
  if(c.sr<1){if(p[2]+c.sr/4+64/4096<0)continue;if(p[0]+m<128-128||p[0]-m>=256||p[1]+m<0||p[1]-m>=200)continue;}
  vis++;nv+=c.v.size;ne+=c.e.length;
  for(const e of c.e){const a=P(e[0]),b=P(e[1]);if(a[2]<0&&b[2]<0)continue;if(Math.max(a[0],b[0])<0||Math.min(a[0],b[0])>=256||Math.max(a[1],b[1])<0||Math.min(a[1],b[1])>=200)continue;win++;}}
 return {nc,vis,nv,ne,win};}
const gs=[grid(30),grid(15),grid(10),grid(5)];
for(const g of gs){let mr=0;for(const c of g.cells)if(c.e.length)mr=Math.max(mr,Math.asin(Math.min(1,c.sr))*180/Math.PI);
 let used=g.cells.filter(c=>c.e.length).length, maxe=Math.max(...g.cells.map(c=>c.e.length)), maxv=Math.max(...g.cells.map(c=>c.v.size)), dupv=g.cells.reduce((a,c)=>a+c.v.size,0);
 console.log(`grid ${g.deg}°: cells ${g.nl*g.nb} (with edges ${used}), max e/v per cell ${maxe}/${maxv}, vertices with duplicates ${dupv}, max radius ${mr.toFixed(1)}°`);}
let seed=777;const rnd=()=>(seed=(seed*1103515245+12345)&0x7fffffff)/0x7fffffff;
const g=gs[1];
const Q14=x=>Math.max(-16384,Math.min(16383,Math.round(x*16384)));
for(const c of g.cells){if(!c.e.length)continue;const bb=[127,-128,127,-128,127,-128];for(const e of c.e)for(const q of e)for(let k=0;k<3;k++){const h=Q14(q[k])>>7;bb[2*k]=Math.min(bb[2*k],h);bb[2*k+1]=Math.max(bb[2*k+1],h);}c.bb=bb;}
function dmax(c,r){const m=r*c.sr*1.125+2, s=(162.5+1.415*m)/r; return s<1?Math.asin(s):Math.PI/2+Math.asin(Math.min(1,c.sr/4+0.016));}
const cellsE=g.cells.filter(c=>c.e.length);
for(const z of [3,4,5]){const r=R[z];const W=[];for(let b=0;b<36;b++)W.push(new Array(g.nb).fill(-1));
 for(let b=0;b<36;b++)for(let s=0;s<=5;s++){const lat0=(-90+5*b+s)*Math.PI/180;
  for(let t=0;t<360;t++){const lon0=t*Math.PI/180,col0=Math.floor(t/15)%24;const v=[Math.cos(lat0)*Math.cos(lon0),Math.cos(lat0)*Math.sin(lon0),Math.sin(lat0)];
   for(const c of cellsE){if(ang(v,c.cc)>dmax(c,r)+0.02)continue;let d=Math.abs(c.col-col0);d=Math.min(d,24-d);W[b][c.row]=Math.max(W[b][c.row],d);}}}
 let cand=0,miss=0,vis=0,thi=0,nv=0;const N=400;
 for(let i=0;i<N;i++){const lon=rnd()*2*Math.PI, lat=(rnd()*2-1)*Math.PI*0.45;const ld=lat*180/Math.PI;const b=Math.min(35,Math.floor((ld+90)/5));
  const col0=Math.floor((lon*180/Math.PI)/15)%24;const bb=[127,-128,127,-128,127,-128];
  for(let row=0;row<g.nb;row++){const w=W[b][row];if(w<0)continue;for(let d=-Math.min(w,12);d<=Math.min(w,11);d++){const c=g.cells[row*24+((col0+d)%24+24)%24];cand++;if(c.e.length)for(let k=0;k<6;k+=2){bb[k]=Math.min(bb[k],c.bb[k]);bb[k+1]=Math.max(bb[k+1],c.bb[k+1]);}}}
  for(let k=0;k<6;k+=2)thi+=Math.max(0,bb[k+1]-bb[k]+1);
  const cl=Math.cos(lon),sl=Math.sin(lon),cC=Math.cos(lat),sC=Math.sin(lat);
  for(const c of cellsE){const W2=c.cc[0]*cl+c.cc[1]*sl;const p=[128+r*(c.cc[1]*cl-c.cc[0]*sl),100+r*(cC*c.cc[2]-sC*W2),cC*W2+sC*c.cc[2]];const m=r*c.sr*1.125+2;
   if(c.sr<1){if(p[2]+c.sr/4+64/4096<0)continue;if(p[0]+m<0||p[0]-m>=256||p[1]+m<0||p[1]-m>=200)continue;}
   vis++;nv+=c.v.size;const w=W[b][c.row];let d=Math.abs(c.col-col0);d=Math.min(d,24-d);if(w<0||d>w)miss++;}}
 console.log(`z${z} 15°: candidates ${(cand/N).toFixed(1)}, visible ${(vis/N).toFixed(1)}, verts ${(nv/N).toFixed(0)}, missed ${miss}, T_hi entries per coord ${(thi/N/3).toFixed(0)} of 256`);}
