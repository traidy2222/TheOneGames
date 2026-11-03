using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.Rendering;
using VenusWorld;
namespace Planetary {
public sealed class PlanetTiles:IDisposable {
 const int Resolution=16,MaxDepth=20,MaxJobs=3,MaxResident=800;
 readonly PlanetSampler sampler;readonly Transform parent;readonly Material material;
 readonly List<Node> roots=new(),all=new();readonly List<Job> jobs=new();
 bool disposed;
 public int ReadyCount {get;private set;}public int VisibleCount {get;private set;}public int DeepestVisible {get;private set;}public int RunningJobs=>jobs.Count;
 public double LastCommitMs{get;private set;}public int ResidentCount=>all.Count;
 sealed class Node {public int face,level;public double u,v,size;public DVector anchor;public Node[] children;public GameObject go;public Mesh mesh;public bool dead,pending;public Vector3[] vertices;}
 sealed class Job {public Node node;public Task<Geometry> task;}
 sealed class Geometry {public Vector3[] vertices,normals,metric;public int[] triangles;}
 public PlanetTiles(PlanetSampler sampler,Transform parent,Material material){this.sampler=sampler;this.parent=parent;this.material=material;for(int i=0;i<6;i++)roots.Add(New(i,0,-1,-1,2));}
 static DVector Face(int face,double u,double v)=>face switch {0=>new DVector(1,v,-u),1=>new DVector(-1,v,u),2=>new DVector(u,1,-v),3=>new DVector(u,-1,v),4=>new DVector(u,v,1),_=>new DVector(-u,v,-1)};
 Node New(int face,int level,double u,double v,double size){var d=Face(face,u+size/2,v+size/2).Normalized;var n=new Node{face=face,level=level,u=u,v=v,size=size,anchor=d*sampler.SurfaceRadius(d)};all.Add(n);return n;}
 void Request(Node n){if(n.dead||n.mesh||n.pending||jobs.Count>=MaxJobs)return;n.pending=true;jobs.Add(new Job{node=n,task=Task.Run(()=>Build(n))});}
 Geometry Build(Node n){
  int side=Resolution+1,count=side*side;var points=new DVector[count];var vertices=new List<Vector3>(count+side*4);var normals=new List<Vector3>();var uv=new List<Vector3>();var indices=new List<int>();
  double ox=n.anchor.x%2048,oy=n.anchor.y%2048,oz=n.anchor.z%2048;
  for(int y=0;y<=Resolution;y++)for(int x=0;x<=Resolution;x++){var d=Face(n.face,n.u+n.size*x/Resolution,n.v+n.size*y/Resolution).Normalized;var p=d*sampler.SurfaceRadius(d);points[y*side+x]=p;var local=p-n.anchor;vertices.Add(local.Float);normals.Add(d.Float);uv.Add(new Vector3((float)(local.x+ox),(float)(local.y+oy),(float)(local.z+oz)));}
  for(int y=0;y<=Resolution;y++)for(int x=0;x<=Resolution;x++){int i=y*side+x;var dx=vertices[y*side+Math.Min(Resolution,x+1)]-vertices[y*side+Math.Max(0,x-1)];var dy=vertices[Math.Min(Resolution,y+1)*side+x]-vertices[Math.Max(0,y-1)*side+x];var normal=Vector3.Cross(dx,dy).normalized;if(Vector3.Dot(normal,normals[i])<0)normal=-normal;normals[i]=normal;}
  for(int y=0;y<Resolution;y++)for(int x=0;x<Resolution;x++){int i=y*side+x;Tri(i,i+1,i+side);Tri(i+1,i+side+1,i+side);}
  // Inward skirts cover cracks at adjacent LODs while the parent stays visible until all children are ready.
  double skirt=Math.Max(.4,n.size*sampler.Radius*.004);
  for(int edge=0;edge<4;edge++){int last=-1,lastOuter=-1;for(int t=0;t<=Resolution;t++){int i=edge switch{0=>t,1=>t*side+Resolution,2=>Resolution*side+Resolution-t,_=>(Resolution-t)*side};int j=vertices.Count;vertices.Add((points[i]-points[i].Normalized*skirt-n.anchor).Float);normals.Add(normals[i]);uv.Add(uv[i]);if(last>=0){Tri(last,i,lastOuter);Tri(i,j,lastOuter);}last=i;lastOuter=j;}}
  return new Geometry{vertices=vertices.ToArray(),normals=normals.ToArray(),metric=uv.ToArray(),triangles=indices.ToArray()};
  void Tri(int a,int b,int c){if(Vector3.Dot(Vector3.Cross(vertices[b]-vertices[a],vertices[c]-vertices[a]),normals[a]+normals[b]+normals[c])<0)(b,c)=(c,b);indices.Add(a);indices.Add(b);indices.Add(c);}
 }
 public void Tick(DVector observer,double scale){
  if(disposed)return;var timer=System.Diagnostics.Stopwatch.StartNew();int committed=0;
  for(int i=jobs.Count-1;i>=0;i--){var job=jobs[i];if(!job.task.IsCompleted)continue;jobs.RemoveAt(i);job.node.pending=false;if(job.node.dead)continue;if(job.task.IsFaulted)throw job.task.Exception;
   var g=job.task.Result;var n=job.node;n.mesh=new Mesh{name=$"Planet tile {n.face}/{n.level}",indexFormat=IndexFormat.UInt32};n.mesh.vertices=g.vertices;n.mesh.normals=g.normals;n.mesh.SetUVs(1,new List<Vector3>(g.metric));n.mesh.triangles=g.triangles;n.mesh.RecalculateBounds();n.vertices=g.vertices;n.go=new GameObject(n.mesh.name);n.go.transform.SetParent(parent,false);n.go.AddComponent<MeshFilter>().sharedMesh=n.mesh;n.go.AddComponent<MeshRenderer>().sharedMaterial=material;n.go.SetActive(false);if(++committed>=2||timer.Elapsed.TotalMilliseconds>3)break;
  }
  LastCommitMs=timer.Elapsed.TotalMilliseconds;
  if(RootsReady){foreach(var root in roots)Refine(root,observer);}else foreach(var root in roots)Request(root);
  VisibleCount=0;ReadyCount=0;DeepestVisible=0;
  foreach(var root in roots)Show(root,true);
  foreach(var n in all)if(n.go){n.go.transform.position=((n.anchor-observer)*(1/scale)).Float;n.go.transform.localScale=Vector3.one*(float)(1/scale);ReadyCount++;}
 }
 void Refine(Node n,DVector observer){
  if(n.dead)return;if(!n.mesh){Request(n);return;}
  double dist=(n.anchor-observer).Magnitude;double span=n.size*sampler.Radius;
  bool split=n.level<MaxDepth&&span>12&&dist<span*.85;
  if(split){if(n.children==null&&all.Count+4<MaxResident){double s=n.size/2;n.children=new[]{New(n.face,n.level+1,n.u,n.v,s),New(n.face,n.level+1,n.u+s,n.v,s),New(n.face,n.level+1,n.u,n.v+s,s),New(n.face,n.level+1,n.u+s,n.v+s,s)};}
   if(n.children!=null){Array.Sort(n.children,(a,b)=>(a.anchor-observer).Magnitude.CompareTo((b.anchor-observer).Magnitude));foreach(var c in n.children)Refine(c,observer);}
  }else if(n.children!=null){foreach(var c in n.children)Release(c);n.children=null;}
 }
 void Show(Node n,bool visible){if(n.dead)return;bool childrenReady=n.children!=null&&Array.TrueForAll(n.children,c=>c.mesh!=null);if(n.go){n.go.SetActive(visible&&!childrenReady);if(visible&&!childrenReady){VisibleCount++;DeepestVisible=Math.Max(DeepestVisible,n.level);}}if(n.children!=null)foreach(var c in n.children)Show(c,visible&&childrenReady);}
 void Release(Node n){n.dead=true;if(n.children!=null)foreach(var c in n.children)Release(c);if(n.go)Destroy(n.go);if(n.mesh)Destroy(n.mesh);all.Remove(n);}
 static void Destroy(UnityEngine.Object o){if(Application.isPlaying)UnityEngine.Object.Destroy(o);else UnityEngine.Object.DestroyImmediate(o);}
 public bool RootsReady=>roots.TrueForAll(n=>n.mesh!=null);
 public bool CoverageValid(){foreach(var root in roots)if(!Covered(root))return false;return true;}
 bool Covered(Node n)=>n.go&&n.go.activeSelf||n.children!=null&&Array.TrueForAll(n.children,Covered);
 public void Dispose(){disposed=true;foreach(var root in roots)Release(root);roots.Clear();}
}
}


