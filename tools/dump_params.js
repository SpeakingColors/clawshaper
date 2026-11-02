// Dumps the mockup parameter table (core.js) to JSON: node tools/dump_params.js <core.js> > tools/params.json
const fs=require('fs');const src=fs.readFileSync(process.argv[2],'utf8');
const a=src.indexOf('const PS={}'),b=src.indexOf('const sys=');
const code=src.slice(a,b);
const f=new Function('clamp',code+';return PS;');
const PS=f();
console.log(JSON.stringify(Object.values(PS),null,0));
