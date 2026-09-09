import {Client} from '@modelcontextprotocol/sdk/client/index.js';
import {StdioClientTransport} from '@modelcontextprotocol/sdk/client/stdio.js';
import {writeFile, mkdir} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const directory = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(directory, '../..');
const output = path.join(root, 'work/mcp');
await mkdir(output, {recursive:true});
const env = {NASA_API_KEY:'DEMO_KEY', PATH:path.dirname(process.execPath),
  SystemRoot:process.env.SystemRoot || 'C:\\Windows', TEMP:path.join(root,'work'), TMP:path.join(root,'work')};
const transport = new StdioClientTransport({command:process.execPath,
  args:[path.join(directory,'node_modules/@programcomputer/nasa-mcp-server/dist/index.js')],
  cwd:output, env, stderr:'pipe'});
const client = new Client({name:'STAR-local-authoring',version:'1.0.0'});
try {
  await client.connect(transport);
  const tools = await client.listTools();
  await writeFile(path.join(output,'tools.json'), JSON.stringify(tools,null,2),'utf8');
  console.log(tools.tools.filter(x=>/images|horizons/.test(x.name)).map(x=>({name:x.name,inputSchema:x.inputSchema})));
  if (process.argv.includes('--call')) {
    const queries = [
      ['nasa_images',{q:'PIA05389',media_type:'image',page_size:1}],
      ['jpl_horizons',{format:'json',COMMAND:"'399'",CENTER:"'500@0'",EPHEM_TYPE:"'VECTORS'",
        START_TIME:"'2026-09-06 00:00:00'",STOP_TIME:"'2026-09-06 00:01:00'",STEP_SIZE:"'1 min'",
        TIME_TYPE:"'UT'",REF_SYSTEM:"'ICRF'",REF_PLANE:"'ECLIPTIC'",OUT_UNITS:"'KM-S'",
        VEC_CORR:"'NONE'",VEC_TABLE:"'2'",CSV_FORMAT:"'YES'"}]
    ];
    const results=[];
    for (const [name,args] of queries) {
      const result=await client.callTool({name,arguments:args},undefined,{timeout:90000});
      await writeFile(path.join(output,name+'.json'),JSON.stringify(result,null,2),'utf8');
      results.push({tool:name,isError:result.isError||false,contentTypes:result.content.map(x=>x.type)});
      console.log(name,results.at(-1));
    }
    await writeFile(path.join(output,'receipt.json'),JSON.stringify({package:'@programcomputer/nasa-mcp-server',version:'1.0.14',transport:'local stdio',personalKeysUsed:false,results},null,2),'utf8');
  }
} finally {await client.close();}
