using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
namespace Excavation {
public sealed class PlanetEnvironment:MonoBehaviour {
 public DigTerrain terrain;public Material water,atmosphere,grass;
 bool built;Mesh vegetation;int edits=-1;float next;readonly List<Mesh> owned=new();
 void Start(){Build();}
 public void Build(){if(built)return;built=true;terrain.Initialize();Shell("Ocean datum (visual only)",terrain.planetRadius-.8f,water);Shell("Atmosphere",terrain.planetRadius+7,atmosphere);if(grass){vegetation=new Mesh{indexFormat=IndexFormat.UInt32};owned.Add(vegetation);var go=new GameObject("Planet meadow");go.transform.SetParent(transform,false);go.AddComponent<MeshFilter>().sharedMesh=vegetation;go.AddComponent<MeshRenderer>().sharedMaterial=grass;Grow();}}
 void Update(){if(built&&vegetation&&edits!=terrain.EditCount&&Time.time>next){next=Time.time+.4f;Grow();}}
 public void RefreshVegetation(){if(vegetation)Grow();}
 void Grow(){var v=new List<Vector3>();var colors=new List<Color>();var ix=new List<int>();var rng=new System.Random(441);for(int i=0;i<18000;i++){float y=1-2*(i+.5f)/18000;float a=i*2.399963f;Vector3 up=new Vector3(Mathf.Sqrt(1-y*y)*Mathf.Cos(a),y,Mathf.Sqrt(1-y*y)*Mathf.Sin(a));float r=terrain.RadiusAt(up);if(r<terrain.planetRadius+.2f)continue;Vector3 p=up*r;if(terrain.Sample(p-up*.2f)>0||terrain.Sample(p+up*.5f)<0)continue;Vector3 tangent=Vector3.Cross(up,Vector3.right).normalized,side=Vector3.Cross(up,tangent);float h=.18f+(float)rng.NextDouble()*.3f;for(int j=0;j<3;j++){float ang=(float)rng.NextDouble()*6.283f;Vector3 s=(tangent*Mathf.Cos(ang)+side*Mathf.Sin(ang))*.035f;Vector3 bend=side*.1f;int k=v.Count;v.Add(p-s-up*.03f);v.Add(p+s-up*.03f);v.Add(p+up*h*.6f-s*.4f+bend*.4f);v.Add(p+up*h*.6f+s*.4f+bend*.4f);v.Add(p+up*h+bend);Color c=Color.Lerp(new Color(.16f,.23f,.08f),new Color(.36f,.39f,.16f),(float)rng.NextDouble());colors.Add(c*.6f);colors.Add(c*.6f);colors.Add(c);colors.Add(c);colors.Add(c*1.2f);ix.AddRange(new[]{k,k+2,k+1,k+1,k+2,k+3,k+2,k+4,k+3});}}vegetation.Clear();vegetation.SetVertices(v);vegetation.SetColors(colors);vegetation.SetTriangles(ix,0);vegetation.RecalculateNormals();vegetation.RecalculateBounds();edits=terrain.EditCount;}
 void Shell(string name,float radius,Material mat){
  var go=new GameObject(name);go.transform.SetParent(transform,false);var mesh=new Mesh{indexFormat=IndexFormat.UInt32};var v=new List<Vector3>();var n=new List<Vector3>();var triangles=new List<int>();const int lat=64,lon=128;
  for(int y=0;y<=lat;y++)for(int x=0;x<=lon;x++){float a=x*Mathf.PI*2/lon,b=y*Mathf.PI/lat;Vector3 d=new Vector3(Mathf.Sin(b)*Mathf.Cos(a),Mathf.Cos(b),Mathf.Sin(b)*Mathf.Sin(a));v.Add(d*radius);n.Add(d);}
  for(int y=0;y<lat;y++)for(int x=0;x<lon;x++){int i=y*(lon+1)+x;triangles.AddRange(new[]{i,i+1,i+lon+1,i+1,i+lon+2,i+lon+1});}
  mesh.SetVertices(v);mesh.SetNormals(n);mesh.SetTriangles(triangles,0);mesh.RecalculateBounds();owned.Add(mesh);go.AddComponent<MeshFilter>().sharedMesh=mesh;var render=go.AddComponent<MeshRenderer>();render.sharedMaterial=mat;render.shadowCastingMode=ShadowCastingMode.Off;
 }
 void OnDestroy(){foreach(var m in owned){if(Application.isPlaying)Destroy(m);else DestroyImmediate(m);}}
}
}
