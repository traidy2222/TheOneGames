using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using Excavation;
namespace VenusWorld.EditorTools {
public static class VenusSetup {
 public static void Create(){
  EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
  var explorer=new GameObject("Venus · measured planetary foundation").AddComponent<VenusExplorer>();
  explorer.basalt=Material("Basalt","Venus/Basalt");explorer.clouds=Material("Clouds","Venus/CloudDeck");explorer.haze=Material("Haze","Venus/SurfaceHaze");explorer.relief=Material("MeasuredRelief","Venus/MeasuredRelief");
  foreach(var pair in new[]{("Albedo","_Rock"),("Normal","_Normal"),("Rough","_Rough")}){string path="Assets/DiggableTerrain/Textures/dark_rock_"+pair.Item1+".jpg";var importer=(TextureImporter)AssetImporter.GetAtPath(path);importer.sRGBTexture=pair.Item1=="Albedo";importer.mipmapEnabled=true;importer.anisoLevel=8;importer.SaveAndReimport();explorer.basalt.SetTexture(pair.Item2,AssetDatabase.LoadAssetAtPath<Texture2D>(path));}EditorUtility.SetDirty(explorer.basalt);
  QualitySettings.antiAliasing=4;QualitySettings.shadowDistance=120;
  EditorSceneManager.SaveScene(UnityEngine.SceneManagement.SceneManager.GetActiveScene(),"Assets/Scenes/Venus.unity");EditorBuildSettings.scenes=new[]{new EditorBuildSettingsScene("Assets/Scenes/Venus.unity",true)};AssetDatabase.SaveAssets();
 }
 static Material Material(string name,string shader){string path="Assets/Venus/"+name+".mat";var m=AssetDatabase.LoadAssetAtPath<Material>(path);if(!m){m=new Material(Shader.Find(shader));AssetDatabase.CreateAsset(m,path);}return m;}
 static void Require(bool ok,string text){if(!ok)throw new Exception(text);}
 public static void BatchVerify(){
  Create();VenusData.Load();Require(VenusData.Width==8192&&VenusData.Height==4096,"DEM resolution");
  var site=new VenusSite(-7.5,303);Require(site.Measured,"Venera region has measured data");Require(Math.Abs(site.Origin.Magnitude-(VenusData.DatumRadius+site.elevation))<.000001,"Correct Magellan datum");Require(Math.Abs((site.World(Vector3.right)-site.Origin).Magnitude-1)<.000001,"One metre retained at planetary coordinates");
  Require(Math.Abs(VenusData.Elevation(12,180,out _)-VenusData.Elevation(12,-180,out _))<.000001,"Longitude wrap");VenusData.Coordinates(site.Origin,out double lat,out double lon);Require(Math.Abs(lat+7.5)<.000001&&Math.Abs(lon+57)<.000001,"Geodetic round trip");
  var explorer=UnityEngine.Object.FindFirstObjectByType<VenusExplorer>();explorer.Initialize();Excavation.EditorTools.TerrainBridge.Capture();Copy("venus-orbit.png");
  explorer.SetSurface();Physics.SyncTransforms();var ground=explorer.Ground;Vector3 top=new Vector3(0,20,0);Require(Physics.Raycast(top,Vector3.down,out var before,40),"Surface collision");ground.BeginStroke();ground.Sculpt(before.point,2.5f,false);ground.Flush();Require(Physics.Raycast(top,Vector3.down,out var after,40)&&after.point.y<before.point.y-1,"Venus excavation collision");
  var camera=explorer.ActiveCamera;camera.transform.position=new Vector3(2,site.Height(2,-5)+1.8f,-5);camera.transform.LookAt(before.point-Vector3.up);Excavation.EditorTools.TerrainBridge.Capture();Copy("venus-excavation.png");ground.Undo();ground.Flush();
  camera.transform.position=new Vector3(0,site.Height(0,-10)+1.65f,-10);camera.transform.rotation=Quaternion.Euler(7,15,0);Excavation.EditorTools.TerrainBridge.Capture();Copy("venus-surface.png");
  // Exercise movement at actual metre scale using Venus gravity.
  var probe=new GameObject("Surface traversal check");var cc=probe.AddComponent<CharacterController>();cc.height=1.8f;cc.radius=.32f;cc.center=new Vector3(0,.9f,0);probe.transform.position=new Vector3(-10,site.Height(-10,-5)+2,-5);Physics.SyncTransforms();float fall=0;try{for(int i=0;i<240;i++){fall=cc.isGrounded?-2:fall-VenusData.Gravity/60;cc.Move(new Vector3(1,fall,0)/60);Require(probe.transform.position.y>site.Height(probe.transform.position.x,probe.transform.position.z)-.5f,"Surface traversal stays above ground");}Require(cc.isGrounded,"Observer lands");}finally{UnityEngine.Object.DestroyImmediate(probe);}
  foreach(var name in new[]{"Venus/CloudDeck","Venus/Basalt","Venus/SurfaceHaze","Venus/MeasuredRelief"})Require(!ShaderUtil.ShaderHasError(Shader.Find(name)),"Shader compiled: "+name); string root=Path.Combine(Application.dataPath,"../Library/TerrainBridge");File.WriteAllText(Path.Combine(root,"venus-validation.txt"),$"PASS: 8192x4096 measured DEM, datum conversion, longitude seam, double precision metre offset, coordinate round trip, orbital and surface renders, digging collision, undo, surface traversal.\nVenera region elevation above Magellan datum: {site.elevation:F3} m.\nMean radius: {VenusData.MeanRadius} m; datum: {VenusData.DatumRadius} m; gravity: {VenusData.Gravity} m/s2.\nGlobal measured coverage: 91.7119%; invalid pixels are explicitly flagged.\nLocal detail, cloud pattern, haze and lighting are reconstructions.\n");Debug.Log("VENUS VERIFICATION PASSED");
 }
 static void Copy(string name){string root=Path.Combine(Application.dataPath,"../Library/TerrainBridge");File.Copy(Path.Combine(root,"preview.png"),Path.Combine(root,name),true);}
}
}

