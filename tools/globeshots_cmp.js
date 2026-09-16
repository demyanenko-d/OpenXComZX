// Сверка снимков tools/globeshots.ps1 двух прогонов в окне глобуса (x < 256): node tools/globeshots_cmp.js ref new
const fs=require("fs"), zlib=require("zlib");
function png(p){const b=fs.readFileSync(p);let o=8,w,h,bd,ct,idat=[];while(o<b.length){const len=b.readUInt32BE(o),t=b.slice(o+4,o+8).toString("latin1");if(t==="IHDR"){w=b.readUInt32BE(o+8);h=b.readUInt32BE(o+12);bd=b[o+16];ct=b[o+17];}else if(t==="IDAT")idat.push(b.slice(o+8,o+8+len));o+=12+len;}
const raw=zlib.inflateSync(Buffer.concat(idat));const bpp=ct===3?1:(ct===2?3:(ct===6?4:1));const rowb=ct===3?Math.ceil(w*bd/8):w*bpp;const out=Buffer.alloc(h*rowb);let prev=Buffer.alloc(rowb);
for(let y=0,pos=0;y<h;y++){const f=raw[pos++];const line=raw.slice(pos,pos+rowb);pos+=rowb;const cur=Buffer.alloc(rowb);for(let i=0;i<rowb;i++){const a=i>=bpp?cur[i-bpp]:0,bb=prev[i],c=i>=bpp?prev[i-bpp]:0;let v=line[i];if(f===1)v+=a;else if(f===2)v+=bb;else if(f===3)v+=(a+bb)>>1;else if(f===4){const pp=a+bb-c,pa=Math.abs(pp-a),pb=Math.abs(pp-bb),pc=Math.abs(pp-c);v+=(pa<=pb&&pa<=pc)?a:(pb<=pc?bb:c);}cur[i]=v&255;}cur.copy(out,y*rowb);prev=cur;}return{w,h,rowb,bpp:rowb/w,data:out};}
const A=process.argv[2]||"ref", B=process.argv[3]||"new"; let bad=0;
for (const n of ["z0","z0r","z1","z2","z3","z3u","z4","z5","z0b"]) {
  const pa=`tmp/shots/asm/${A}-${n}.png`, pb=`tmp/shots/asm/${B}-${n}.png`;
  if (!fs.existsSync(pa)||!fs.existsSync(pb)) { console.log(n, "нет снимка"); bad++; continue; }
  const a=png(pa), b=png(pb); let d=0;
  for(let y=0;y<a.h;y++)for(let x=0;x<256;x++){for(let k=0;k<a.bpp;k++)if(a.data[y*a.rowb+x*a.bpp+k]!==b.data[y*b.rowb+x*b.bpp+k]){d++;break;}}
  console.log(n.padEnd(4), d ? "РАЗЛИЧИЙ "+d : "совпадает"); if (d) bad++;
}
process.exit(bad ? 1 : 0);
