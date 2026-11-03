using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
namespace Excavation {
public sealed class LandscapeEnvironment:MonoBehaviour {
 public DigTerrain terrain; public Material grassMaterial;
 Mesh grassMesh; int edits=-1;float next;bool built;
 readonly List<Mesh> owned=new();
 void Start(){Build();}
 public void Build(){
  if(built)return;built=true;terrain.Initialize();
  var verts=new List<Vector3>();var colors=new List<Color>();var tris=new List<int>();
  const int n=160;const float step=4;
  for(int z=0;z<=n;z++)for(int x=0;x<=n;x++){float px=(x-n/2)*step,pz=(z-n/2)*step;verts.Add(new Vector3(px,terrain.Surface(px,pz)-.08f,pz));colors.Add(new Color(0,1,0,1));}
  for(int z=0;z<n;z++)for(int x=0;x<n;x++){
   float px=(x-n/2)*step,pz=(z-n/2)*step;
   if(px>=-32&&px<32&&pz>=-32&&pz<32)continue;
   int i=z*(n+1)+x;tris.AddRange(new[]{i,i+n+1,i+1,i+1,i+n+1,i+n+2});
  }
  var mesh=new Mesh{indexFormat=IndexFormat.UInt32};mesh.SetVertices(verts);mesh.SetColors(colors);mesh.SetTriangles(tris,0);mesh.RecalculateNormals();mesh.RecalculateBounds();owned.Add(mesh);RenderMesh("Distant highlands (scenery)",mesh,terrain.material);
  grassMesh=new Mesh{indexFormat=IndexFormat.UInt32};grassMesh.MarkDynamic();owned.Add(grassMesh);RenderMesh("Meadow grass",grassMesh,grassMaterial);RebuildGrass();
  var random=new System.Random(919);
  for(int i=0;i<55;i++){
   float angle=(float)random.NextDouble()*Mathf.PI*2,dist=42+(float)random.NextDouble()*140;
   float x=Mathf.Cos(angle)*dist,z=Mathf.Sin(angle)*dist;
   var rock=GameObject.CreatePrimitive(PrimitiveType.Sphere);rock.name="Weathered outcrop";rock.transform.SetParent(transform);rock.transform.position=new Vector3(x,terrain.Surface(x,z)-.6f,z);rock.transform.localScale=new Vector3(2+(float)random.NextDouble()*5,1+(float)random.NextDouble()*3,2+(float)random.NextDouble()*4);rock.transform.rotation=Quaternion.Euler(0,(float)random.NextDouble()*360,12);
   var col=rock.GetComponent<Collider>();if(Application.isPlaying)Destroy(col);else DestroyImmediate(col);
   var m=Instantiate(rock.GetComponent<MeshFilter>().sharedMesh);var v=m.vertices;var c=new Color[v.Length];for(int j=0;j<v.Length;j++){v[j]*=.85f+Mathf.PerlinNoise(v[j].x*9+i,v[j].y*9+v[j].z*5)*.3f;c[j]=Color.red;}m.vertices=v;m.colors=c;m.RecalculateNormals();owned.Add(m);rock.GetComponent<MeshFilter>().sharedMesh=m;rock.GetComponent<MeshRenderer>().sharedMaterial=terrain.material;
  }
 }
 void RenderMesh(string name,Mesh mesh,Material mat){var go=new GameObject(name);go.transform.SetParent(transform,false);go.AddComponent<MeshFilter>().sharedMesh=mesh;go.AddComponent<MeshRenderer>().sharedMaterial=mat;}
 void Update(){if(built&&Time.time>=next&&edits!=terrain.EditCount){next=Time.time+.3f;RebuildGrass();}}
 void RebuildGrass(){
  var random=new System.Random(318);var v=new List<Vector3>();var c=new List<Color>();var indices=new List<int>();
  for(int i=0;i<8500;i++){
   float x=((float)random.NextDouble()-.5f)*150,z=((float)random.NextDouble()-.5f)*150;float y=terrain.Surface(x,z);
   float slope=Mathf.Abs(terrain.Surface(x+.4f,z)-y)+Mathf.Abs(terrain.Surface(x,z+.4f)-y);
   float patch=Mathf.PerlinNoise(x*.14f+51,z*.14f+27);float h=.2f+(float)random.NextDouble()*.45f;
   if(slope>.45f||patch<.4f)continue;
   if(Mathf.Abs(x)<34&&Mathf.Abs(z)<34&&(terrain.Sample(new Vector3(x,y-.18f,z))>0||terrain.Sample(new Vector3(x,y+.4f,z))<0))continue;
   for(int blade=0;blade<4;blade++){
    float a=(float)random.NextDouble()*Mathf.PI*2;Vector3 side=new Vector3(Mathf.Cos(a),0,Mathf.Sin(a))*.045f;Vector3 bend=new Vector3(Mathf.Sin(a),0,Mathf.Cos(a))*.16f;Vector3 b=new Vector3(x,y-.04f,z);
    int k=v.Count;v.Add(b-side);v.Add(b+side);v.Add(b+Vector3.up*h*.6f+bend*.3f-side*.45f);v.Add(b+Vector3.up*h*.6f+bend*.3f+side*.45f);v.Add(b+Vector3.up*h+bend);
    var color=Color.Lerp(new Color(.18f,.23f,.08f),new Color(.42f,.43f,.19f),(float)random.NextDouble());c.Add(color*.65f);c.Add(color*.65f);c.Add(color);c.Add(color);c.Add(color*1.2f);
    indices.AddRange(new[]{k,k+2,k+1,k+1,k+2,k+3,k+2,k+4,k+3});
   }
  }
  grassMesh.Clear();grassMesh.SetVertices(v);grassMesh.SetColors(c);grassMesh.SetTriangles(indices,0);grassMesh.RecalculateNormals();grassMesh.RecalculateBounds();edits=terrain.EditCount;
 }
 void OnDestroy(){foreach(var m in owned){if(Application.isPlaying)Destroy(m);else DestroyImmediate(m);}}
}
}
