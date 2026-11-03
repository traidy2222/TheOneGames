using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
namespace Excavation.EditorTools {
public static class PlanetSetup {
 public static void Create(){
  TerrainSetup.Create();
  UnityEngine.Object.DestroyImmediate(UnityEngine.Object.FindFirstObjectByType<LandscapeEnvironment>().gameObject);
  var t=UnityEngine.Object.FindFirstObjectByType<DigTerrain>();t.spherical=true;t.cellsX=t.cellsY=t.cellsZ=128;t.spacing=1.2f;t.planetRadius=60;
  t.material=Asset("Planet Ground",new Material(t.material));t.material.SetFloat("_Planet",1);EditorUtility.SetDirty(t.material);
  var env=new GameObject("Planet atmosphere and ocean").AddComponent<PlanetEnvironment>();env.terrain=t;env.grass=AssetDatabase.LoadAssetAtPath<Material>("Assets/DiggableTerrain/Meadow.mat");env.water=Asset("Ocean",new Material(Shader.Find("Excavation/Ocean")));env.atmosphere=Asset("Atmosphere",new Material(Shader.Find("Excavation/Atmosphere")));
  var sky=Asset("Planet Sky",new Material(Shader.Find("Excavation/PlanetSky")));sky.SetFloat("_Radius",t.planetRadius);RenderSettings.skybox=sky;RenderSettings.fog=false;
  var cam=Camera.main;cam.farClipPlane=1500;cam.nearClipPlane=.08f;
  var player=cam.transform.parent;Vector3 d=t.SpawnDirection();player.position=d*(t.RadiusAt(d)+2);player.rotation=Quaternion.FromToRotation(Vector3.up,d);
  EditorSceneManager.SaveScene(UnityEngine.SceneManagement.SceneManager.GetActiveScene(),"Assets/Scenes/Planet.unity");
  EditorBuildSettings.scenes=new[]{new EditorBuildSettingsScene("Assets/Scenes/Planet.unity",true),new EditorBuildSettingsScene("Assets/Scenes/Excavation.unity",true)};AssetDatabase.SaveAssets();
 }
 static Material Asset(string name,Material material){string path="Assets/DiggableTerrain/"+name+".mat";var old=AssetDatabase.LoadAssetAtPath<Material>(path);if(old){EditorUtility.CopySerialized(material,old);UnityEngine.Object.DestroyImmediate(material);return old;}AssetDatabase.CreateAsset(material,path);return material;}
 public static void Validate(){
  var go=new GameObject("Planet verification");var t=go.AddComponent<DigTerrain>();t.spherical=true;t.cellsX=t.cellsY=t.cellsZ=64;t.spacing=1;t.planetRadius=22;
  try {t.Initialize();Vector3[] dirs={Vector3.up,Vector3.down,Vector3.left,Vector3.right,Vector3.forward,Vector3.back};
   foreach(var d in dirs){Require(t.Sample(d*6)<0,"Solid interior on "+d);Require(t.Sample(d*31)>0,"Empty exterior");Require(Physics.Raycast(d*40,-d,out var h,40)&&h.collider.transform.IsChildOf(go.transform),"Radial surface collision");float radius=h.point.magnitude;t.BeginStroke();t.Sculpt(h.point,2.5f,false);t.Flush();Require(Physics.Raycast(d*40,-d,out var cut,40)&&cut.point.magnitude<radius-1,"Dig radial face "+d);t.Undo();t.Flush();}
   var p=Vector3.down*15;t.Sculpt(p,2,false);t.Flush();Require(t.Sample(p)>1&&t.Sample(p+Vector3.down*4)<0,"Southern underground cavity with roof");
   Require(Physics.Raycast(p,Vector3.down,out var cave,5),"Cavity collision");
   Vector3 at=Vector3.up*t.RadiusAt(Vector3.up);t.Deposit(at,3);t.Flush();Require(t.Sample(at+Vector3.up*.5f)<0,"Radial deposited mound");Require(t.LastChangedVolume>0,"Deposit volume estimate");
   t.Smooth(at,2);t.Flush();foreach(var f in go.GetComponentsInChildren<MeshFilter>())Require(f.sharedMesh.colors.Length==f.sharedMesh.vertexCount,"Material weights on every vertex");
   string file=Path.Combine(Application.temporaryCachePath,"planet-check.bin");
   try{t.Save(file);float saved=t.Sample(Vector3.up*10);t.Sculpt(Vector3.up*10,2,false);t.Load(file);Require(Mathf.Abs(t.Sample(Vector3.up*10)-saved)<.001f,"Planet save round trip");t.planetRadius+=1;bool rejected=false;try{t.Load(file);}catch(InvalidDataException){rejected=true;}t.planetRadius-=1;Require(rejected,"Reject different planet radius");}finally{if(File.Exists(file))File.Delete(file);}
   Debug.Log("PLANET VALIDATION PASSED: six radial faces, excavation collision, underground cavity, radial mound, smoothing, material weights.");
  }finally{UnityEngine.Object.DestroyImmediate(go);}
 }
 static void Require(bool result,string name){if(!result)throw new Exception(name);}
 public static void BatchVerify(){
  Create();TerrainSetup.Validate();Validate();
  var t=UnityEngine.Object.FindFirstObjectByType<DigTerrain>();var timer=System.Diagnostics.Stopwatch.StartNew();t.Initialize();Debug.Log("PLANET INITIALIZATION MS: "+timer.Elapsed.TotalMilliseconds);
  UnityEngine.Object.FindFirstObjectByType<PlanetEnvironment>().Build();
  foreach(Vector3 start in new[]{t.SpawnDirection(),Vector3.up,Vector3.down,Vector3.right,Vector3.left,Vector3.forward,Vector3.back}){
   var walker=new GameObject("Radial walker verification");var motor=walker.AddComponent<PlanetMotor>();walker.transform.position=start*(t.RadiusAt(start)+2);walker.transform.rotation=Quaternion.FromToRotation(Vector3.up,start);float fall=0;Physics.SyncTransforms();
   try{for(int i=0;i<300;i++){Vector3 up=walker.transform.position.normalized;walker.transform.rotation=Quaternion.FromToRotation(walker.transform.up,up)*walker.transform.rotation;fall=motor.Grounded?-2:fall-.333f;motor.Move(walker.transform.forward*2+up*fall,1f/60);Require(walker.transform.position.magnitude>t.RadiusAt(walker.transform.position)-.5f,"Walker remains above terrain "+start);}
    Require(Vector3.Distance(walker.transform.position,start*(t.RadiusAt(start)+2))>4,"Walker traverses spherical ground");Require(motor.Grounded,"Walker lands on radial surface");
   }finally{UnityEngine.Object.DestroyImmediate(walker);}}
  Debug.Log("RADIAL WALKER CHECK PASSED: spawn and six hemispheres");
  var cam=Camera.main;cam.transform.position=new Vector3(.4f,.6f,-.7f).normalized*160;cam.transform.LookAt(Vector3.zero,Vector3.up);TerrainBridge.Capture();
  string root=Path.Combine(Application.dataPath,"../Library/TerrainBridge");File.Copy(Path.Combine(root,"preview.png"),Path.Combine(root,"planet-orbit.png"),true);
  Vector3 d=t.SpawnDirection();Vector3 surface=d*t.RadiusAt(d);timer.Restart();t.Sculpt(surface,4,false);t.Flush();Debug.Log("PLANET CUT MS: "+timer.Elapsed.TotalMilliseconds);UnityEngine.Object.FindFirstObjectByType<PlanetEnvironment>().RefreshVegetation();
  Vector3 tangent=Vector3.Cross(d,Vector3.right).normalized;cam.transform.position=surface+d*3-tangent*8;cam.transform.LookAt(surface-d, d);TerrainBridge.Capture();File.Copy(Path.Combine(root,"preview.png"),Path.Combine(root,"planet-surface.png"),true);
  Debug.Log("PLANET RENDER CHECK PASSED");
 }
}
}
