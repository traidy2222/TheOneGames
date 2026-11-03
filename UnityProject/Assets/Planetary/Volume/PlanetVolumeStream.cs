using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.Rendering;
using VenusWorld;
namespace Planetary {
public sealed class PlanetVolumeStream:IDisposable {
 sealed class Node {public VolumeKey key;public GameObject go;public Mesh mesh;public VolumeMesher.Geometry geometry;public int version,built=-1,lit=-1;public bool pending,visible,core;}
 sealed class Job {public Node node;public int version,lighting;public Task<Result> task;}
 sealed class Result {public VolumeMesher.Geometry geometry;public Color[] light;public bool geometryChanged;}
 readonly PlanetDensityStore store;readonly Transform parent;readonly Material material;readonly Dictionary<VolumeKey,Node> nodes=new();readonly List<Job> jobs=new();readonly Vector3 sun;readonly Color sky;
 readonly double viewDistance;readonly int budget;readonly ComputeBuffer coverage;readonly Vector4Int[] hash=new Vector4Int[8192];
 bool disposed,hasBounds,coverageDirty=true,viewDirty=true;DVector minimum,maximum,lastView;int lightingVersion;Task<DensityReadView> viewTask;DensityReadView lightView;double nextSelection;VolumeKey[] farKeys=Array.Empty<VolumeKey>();
 public bool Ready=>hasBounds;public bool Settled{get;private set;}public bool AllSettled{get;private set;}public int ResidentCount=>nodes.Count;public int Pending=>jobs.Count+(viewTask!=null?1:0);public int VisibleCount{get;private set;}public double ViewDistance=>viewDistance;public DensityReadView LightingView=>lightView;
 // Explicit integer layout is shared with the shader's int4 hash table.
 [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]struct Vector4Int {public int x,y,z,w;public Vector4Int(int x,int y,int z,int w){this.x=x;this.y=y;this.z=z;this.w=w;}}
 public PlanetVolumeStream(PlanetDensityStore store,Transform parent,Material material,double viewDistance=4096,int budget=1024,Vector3? sun=null,Color? sky=null){this.store=store;this.parent=parent;this.material=material;this.viewDistance=Math.Clamp(viewDistance,128,16384);this.budget=Math.Clamp(budget,64,4096);this.sun=(sun??new Vector3(.6f,.25f,-.75f)).normalized;this.sky=sky??new Color(.56f,.43f,.28f);coverage=new ComputeBuffer(8192,16);coverage.SetData(hash);Shader.SetGlobalBuffer("_VolumeCoverage",coverage);store.Changed+=Invalidate;}
 public bool Contains(DVector p,double margin=0)=>hasBounds&&p.x>minimum.x+margin&&p.y>minimum.y+margin&&p.z>minimum.z+margin&&p.x<maximum.x-margin&&p.y<maximum.y-margin&&p.z<maximum.z-margin;
 public bool IsVisible(VolumeKey key)=>nodes.TryGetValue(key,out var n)&&n.visible&&n.mesh;
 void Invalidate(DVector lo,DVector hi){viewDirty=true;lightingVersion++;nextSelection=0;foreach(var node in nodes.Values){var o=node.key.Origin;if(o.x<=hi.x&&o.y<=hi.y&&o.z<=hi.z&&o.x+16>=lo.x&&o.y+16>=lo.y&&o.z+16>=lo.z)node.version++;}}
 public void Tick(DVector observer,double scale,bool active){if(disposed)return;
  if(viewTask!=null&&viewTask.IsCompleted){if(viewTask.IsFaulted)throw viewTask.Exception;lightView=viewTask.Result;viewTask=null;}
  if(active&&viewTask==null&&(viewDirty||lightView==null||(observer-lastView).Magnitude>128)){viewDirty=false;lastView=observer;var position=observer;viewTask=Task.Run(()=>store.ReadView(position,viewDistance*2+64));}
  for(int i=jobs.Count-1;i>=0;i--){var job=jobs[i];if(!job.task.IsCompleted)continue;jobs.RemoveAt(i);var node=job.node;node.pending=false;if(job.task.IsFaulted)throw job.task.Exception;if(!nodes.TryGetValue(node.key,out var current)||!ReferenceEquals(current,node)||job.version!=node.version)continue;Commit(node,job.task.Result);node.built=job.version;node.lit=job.task.Result.geometryChanged?-1:job.lighting;coverageDirty=true;break;}
  if(!active){Settled=AllSettled=false;hasBounds=false;foreach(var node in nodes.Values)Release(node);nodes.Clear();coverageDirty=true;UpdateCoverage(observer,scale);return;}
  var center=VolumeKey.At(observer);var wanted=new HashSet<VolumeKey>();var core=new List<Node>();var queue=new List<Node>();foreach(var node in nodes.Values)node.core=false;
  for(long z=center.z-1;z<=center.z+1;z++)for(long y=center.y-1;y<=center.y+1;y++)for(long x=center.x-1;x<=center.x+1;x++){var node=Want(new VolumeKey(x,y,z));node.core=true;core.Add(node);}
  double now=Time.realtimeSinceStartupAsDouble;if(now>nextSelection||farKeys.Length==0){farKeys=store.EditedKeys(observer,viewDistance);nextSelection=now+1;}
  var distant=new HashSet<VolumeKey>();foreach(var key in farKeys)for(int z=-1;z<=0;z++)for(int y=-1;y<=0;y++)for(int x=-1;x<=0;x++){var k=new VolumeKey(key.x+x,key.y+y,key.z+z);if((k.Origin+new DVector(8,8,8)-observer).Magnitude<=viewDistance+28)distant.Add(k);}
  var ordered=new List<VolumeKey>(distant);ordered.Sort((a,b)=>(a.Origin-observer).Magnitude.CompareTo((b.Origin-observer).Magnitude));foreach(var key in ordered){if(wanted.Count>=budget)break;Want(key);}
  queue.Sort((a,b)=>{int pa=a.core?(a.built!=a.version?0:1):(a.built!=a.version?2:3),pb=b.core?(b.built!=b.version?0:1):(b.built!=b.version?2:3);return pa!=pb?pa.CompareTo(pb):(a.key.Origin-observer).Magnitude.CompareTo((b.key.Origin-observer).Magnitude);});
  if(lightView!=null&&viewTask==null)foreach(var node in queue){if(jobs.Count>=2)break;bool geometryChanged=node.built!=node.version;bool relight=node.lit!=lightingVersion;if(node.pending||(!geometryChanged&&!relight))continue;node.pending=true;var key=node.key;var existing=node.geometry;var view=lightView;jobs.Add(new Job{node=node,version=node.version,lighting=lightingVersion,task=Task.Run(()=>{var g=geometryChanged?VolumeMesher.Build(key,store.Snapshot(key)):existing;return new Result{geometry=g,geometryChanged=geometryChanged,light=geometryChanged?new Color[g.vertices.Length]:VoxelLighting.Bake(key,g,view,sun,sky,viewDistance)};})});}
  Settled=core.TrueForAll(n=>n.built==n.version);AllSettled=queue.TrueForAll(n=>n.built==n.version&&n.lit==lightingVersion)&&jobs.Count==0&&viewTask==null;
  if(Settled){minimum=new VolumeKey(center.x-1,center.y-1,center.z-1).Origin;maximum=new VolumeKey(center.x+2,center.y+2,center.z+2).Origin;hasBounds=true;}
  var remove=new List<VolumeKey>();VisibleCount=0;foreach(var pair in nodes){var node=pair.Value;var o=node.key.Origin;bool retained=hasBounds&&o.x>=minimum.x&&o.y>=minimum.y&&o.z>=minimum.z&&o.x<maximum.x&&o.y<maximum.y&&o.z<maximum.z;if(!wanted.Contains(pair.Key)&&!retained)remove.Add(pair.Key);else if(node.go){bool visible=node.built>=0&&(retained||distant.Contains(node.key));if(node.visible!=visible){node.visible=visible;coverageDirty=true;}node.go.SetActive(visible);node.go.transform.position=((o-observer)*(1/scale)).Float;node.go.transform.localScale=Vector3.one*(float)(1/scale);var collider=node.go.GetComponent<MeshCollider>();if(node.core&&node.geometry.triangles.Length>0){if(collider.sharedMesh!=node.mesh)collider.sharedMesh=node.mesh;collider.enabled=true;}else{collider.enabled=false;collider.sharedMesh=null;}if(visible)VisibleCount++;}}
  foreach(var key in remove){Release(nodes[key]);nodes.Remove(key);coverageDirty=true;}UpdateCoverage(observer,scale);
  Node Want(VolumeKey key){if(!nodes.TryGetValue(key,out var node)){node=new Node{key=key};nodes.Add(key,node);}if(wanted.Add(key))queue.Add(node);return node;}
 }
 static uint Hash(int x,int y,int z)=>unchecked(((uint)x*73856093u)^((uint)y*19349663u)^((uint)z*83492791u))&8191u;
 void UpdateCoverage(DVector observer,double scale){var key=VolumeKey.At(observer);var offset=observer-key.Origin;Shader.SetGlobalVector("_ObserverChunk",new Vector4((float)key.x,(float)key.y,(float)key.z,0));Shader.SetGlobalVector("_ObserverInChunk",new Vector4((float)offset.x,(float)offset.y,(float)offset.z,(float)scale));if(coverageDirty){Array.Clear(hash,0,hash.Length);foreach(var node in nodes.Values)if(node.visible){int x=(int)node.key.x,y=(int)node.key.y,z=(int)node.key.z;uint slot=Hash(x,y,z);for(int i=0;i<8192;i++,slot=(slot+1)&8191)if(hash[slot].w==0){hash[slot]=new Vector4Int(x,y,z,1);break;}}coverage.SetData(hash);coverageDirty=false;}Shader.SetGlobalFloat("_VolumeCoverageReady",hasBounds?1:0);}
 void Commit(Node node,Result result){var g=result.geometry;if(!node.go){node.go=new GameObject("Volume "+node.key);node.go.transform.SetParent(parent,false);node.go.AddComponent<MeshFilter>();node.go.AddComponent<MeshRenderer>().sharedMaterial=material;node.go.AddComponent<MeshCollider>();node.go.SetActive(false);}if(!result.geometryChanged&&node.mesh){node.mesh.colors=result.light;return;}var mesh=new Mesh{name="Planet volume "+node.key,indexFormat=IndexFormat.UInt32};mesh.vertices=g.vertices;mesh.normals=g.normals;mesh.SetUVs(1,new List<Vector3>(g.metric));mesh.colors=result.light;mesh.triangles=g.triangles;mesh.RecalculateBounds();node.go.GetComponent<MeshFilter>().sharedMesh=mesh;node.go.GetComponent<MeshCollider>().sharedMesh=null;if(node.mesh)Destroy(node.mesh);node.mesh=mesh;node.geometry=g;}
 static void Destroy(UnityEngine.Object o){if(Application.isPlaying)UnityEngine.Object.Destroy(o);else UnityEngine.Object.DestroyImmediate(o);}
 static void Release(Node n){if(n.go)Destroy(n.go);if(n.mesh)Destroy(n.mesh);}
 public void Dispose(){if(disposed)return;disposed=true;store.Changed-=Invalidate;foreach(var n in nodes.Values)Release(n);nodes.Clear();coverage.Release();Shader.SetGlobalFloat("_VolumeCoverageReady",0);}
}
}
