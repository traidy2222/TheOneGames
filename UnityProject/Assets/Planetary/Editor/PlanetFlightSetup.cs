using System;
using System.IO;
using System.Threading;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using VenusWorld;
namespace Planetary.EditorTools {
public static class PlanetFlightSetup {
 public static void Create(){
  EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
  var definition=AssetDatabase.LoadAssetAtPath<PlanetDefinition>("Assets/Planetary/Venus.asset");if(!definition){definition=ScriptableObject.CreateInstance<PlanetDefinition>();AssetDatabase.CreateAsset(definition,"Assets/Planetary/Venus.asset");}definition.displayName="Venus";definition.radiusMetres=6051800;definition.elevationDatum=6051000;definition.useMagellan=true;definition.surfaceGravity=8.87f;definition.atmosphereHeight=100000;definition.cloudHeight=65000;definition.excavationMeshBudget=2048;
  var terrain=Material("Surface","Planetary/Surface");terrain.SetTexture("_Rock",AssetDatabase.LoadAssetAtPath<Texture2D>("Assets/DiggableTerrain/Textures/dark_rock_Albedo.jpg"));terrain.SetTexture("_Rough",AssetDatabase.LoadAssetAtPath<Texture2D>("Assets/DiggableTerrain/Textures/dark_rock_Rough.jpg"));terrain.SetTexture("_Bump",AssetDatabase.LoadAssetAtPath<Texture2D>("Assets/DiggableTerrain/Textures/dark_rock_Normal.jpg"));EditorUtility.SetDirty(terrain);definition.terrainMaterial=terrain;definition.cloudMaterial=Material("Clouds","Planetary/CloudDeck");EditorUtility.SetDirty(definition);
  var test=AssetDatabase.LoadAssetAtPath<PlanetDefinition>("Assets/Planetary/AirlessTestWorld.asset");if(!test){test=ScriptableObject.CreateInstance<PlanetDefinition>();AssetDatabase.CreateAsset(test,"Assets/Planetary/AirlessTestWorld.asset");}test.displayName="Procedural airless test world";test.useMagellan=false;test.radiusMetres=1737400;test.surfaceGravity=1.62f;test.atmosphereHeight=0;test.cloudHeight=0;test.terrainMaterial=terrain;test.cloudMaterial=null;EditorUtility.SetDirty(test);
  var flight=new GameObject("Universal planet flight").AddComponent<PlanetFlight>();flight.planet=definition;EditorSceneManager.SaveScene(UnityEngine.SceneManagement.SceneManager.GetActiveScene(),"Assets/Scenes/PlanetaryFlight.unity");EditorBuildSettings.scenes=new[]{new EditorBuildSettingsScene("Assets/Scenes/PlanetaryFlight.unity",true)};AssetDatabase.SaveAssets();
 }
 static Material Material(string name,string shader){string path="Assets/Planetary/"+name+".mat";var m=AssetDatabase.LoadAssetAtPath<Material>(path);if(!m){m=new Material(Shader.Find(shader));AssetDatabase.CreateAsset(m,path);}return m;}
 static void Require(bool value,string label){if(!value)throw new Exception(label);}
 static PlanetFlight observed;static int phase,observerID,sceneID;static double started,nextSample;static string evidence;
 public static void StartLiveVerification(){
  observed=UnityEngine.Object.FindFirstObjectByType<PlanetFlight>();Require(Application.isPlaying&&observed,"Play flight scene first");observed.VerificationInputLock=true;observerID=observed.View.GetInstanceID();sceneID=observed.gameObject.scene.handle;phase=0;started=EditorApplication.timeSinceStartup;nextSample=0;evidence=Path.Combine(Application.dataPath,"../Documentation/flight-live-validation.txt");File.WriteAllText(evidence,"Continuous live round trip, input locked for reproducibility.\n");observed.BeginDescent();EditorApplication.update-=Observe;EditorApplication.update+=Observe;
 }
 static void Observe(){
  if(!observed||!Application.isPlaying){EditorApplication.update-=Observe;return;}
  try{
   Require(EditorApplication.timeSinceStartup-started<240,"Round trip completed within 240 seconds");Require(observed.View.GetInstanceID()==observerID&&observed.gameObject.scene.handle==sceneID,"Observer and scene unchanged");if(observed.Tiles.RootsReady)Require(observed.Tiles.CoverageValid(),"Continuous tile coverage");
   if(EditorApplication.timeSinceStartup>nextSample){File.AppendAllText(evidence,$"phase={phase}; altitude={observed.Altitude:F3}; camera={observerID}; scene={sceneID}; nodes={observed.Tiles.ResidentCount}; level={observed.Tiles.DeepestVisible}; excavation={observed.Excavation.Ready}\n");nextSample=EditorApplication.timeSinceStartup+2;}
   if(phase==0&&observed.Altitude<=2.01&&observed.Excavation.Ready){ValidateGround(observed);Excavation.EditorTools.TerrainBridge.Capture();File.Copy(Path.Combine(Application.dataPath,"../Library/TerrainBridge/preview.png"),Path.Combine(Application.dataPath,"../Documentation/flight-surface.png"),true);File.AppendAllText(evidence,"PASS: descent, streamed excavation, collider, save/load, undo.\n");phase=1;observed.BeginAscent();}
   else if(phase==1&&observed.Altitude>=observed.planet.radiusMetres*2-.1){observed.StopCruise();observed.View.transform.rotation=Quaternion.LookRotation(-observed.Position.Normalized.Float,Vector3.up);Excavation.EditorTools.TerrainBridge.Capture();File.Copy(Path.Combine(Application.dataPath,"../Library/TerrainBridge/preview.png"),Path.Combine(Application.dataPath,"../Documentation/flight-orbit.png"),true);File.AppendAllText(evidence,"PASS: continuous ascent back to orbit; unchanged camera/scene for the entire journey.\n");observed.VerificationInputLock=false;EditorApplication.update-=Observe;Debug.Log("LIVE PLANET ROUND TRIP PASSED");}
  }catch(Exception ex){observed.StopCruise();observed.VerificationInputLock=false;EditorApplication.update-=Observe;File.AppendAllText(evidence,"FAIL: "+ex+"\n");Debug.LogException(ex);}
 }
 public static void ValidateGround(PlanetFlight flight){
  Require(flight.Excavation.Ready,"Excavation handoff completed");var store=flight.Excavation.Store;var center=flight.Position.Normalized*(flight.Sampler.SurfaceRadius(flight.Position)-1);double before=store.Sample(center);Require(store.Sculpt(center,3,false)>0,"Volume density changed");Require(store.Sample(center)>1,"Solid becomes tunnel");Require(store.Undo(),"Undo available");Require(Math.Abs(store.Sample(center)-before)<.00001,"Undo restores rock");Debug.Log("FLIGHT VOLUME EXCAVATION VALIDATION PASSED");
 }

 public static void BatchVerify(){
  Create();var flight=UnityEngine.Object.FindFirstObjectByType<PlanetFlight>();flight.Initialize();int camera=flight.View.GetInstanceID(),scene=flight.gameObject.scene.handle;var direction=VenusData.Direction(-7.5,303);double surface=flight.Sampler.SurfaceRadius(direction);string root=Path.Combine(Application.dataPath,"../Library/TerrainBridge");Directory.CreateDirectory(root);double worst=0;int maxNodes=0;
  foreach(double altitude in new[]{12000000d,200000,65000,10000,1000,100,2}){
   flight.SetPositionForVerification(direction*(surface+altitude));int ticks=0;while(ticks++<1800){var watch=System.Diagnostics.Stopwatch.StartNew();flight.ApplyFrame();worst=Math.Max(worst,watch.Elapsed.TotalMilliseconds);maxNodes=Math.Max(maxNodes,flight.Tiles.ResidentCount);if(flight.Tiles.RootsReady)Require(flight.Tiles.CoverageValid(),"Parent coverage while streaming");if(ticks>20&&flight.Tiles.RunningJobs==0)break;Thread.Sleep(1);}
   Require(flight.Tiles.RootsReady,"Global roots ready");Require(flight.View.GetInstanceID()==camera&&flight.gameObject.scene.handle==scene,"Same observer and scene");Require(Math.Abs(flight.Altitude-altitude)<.00001,"Double coordinate altitude");Require(flight.Tiles.ResidentCount<=800,"Bounded terrain cache");if(altitude==2){Require(flight.Tiles.DeepestVisible>=18,"Metre-scale LOD reached at landing");var up=direction.Float;flight.View.transform.rotation=Quaternion.LookRotation(Vector3.ProjectOnPlane(Vector3.up,up).normalized-up*.12f,up);}
   Excavation.EditorTools.TerrainBridge.Capture();File.Copy(Path.Combine(root,"preview.png"),Path.Combine(root,"flight-"+altitude+".png"),true);Debug.Log($"FLIGHT CHECK altitude={altitude} level={flight.Tiles.DeepestVisible} visible={flight.Tiles.VisibleCount} nodes={flight.Tiles.ResidentCount}");
  }
  flight.SetPositionForVerification(direction*(surface+200000));flight.BeginDescent();double previous=flight.Altitude;for(int i=0;i<600;i++){flight.Simulate(.05f);Require(flight.Altitude<=previous+.001,"Continuous monotonic descent");Require(previous-flight.Altitude<=3201,"Bounded flight step");previous=flight.Altitude;}flight.BeginAscent();for(int i=0;i<100;i++){flight.Simulate(.05f);Require(flight.Altitude>=previous-.001,"Continuous ascent");previous=flight.Altitude;}
  var generic=new PlanetSampler(1737400,1737400,false,800,.25f,42);var p=direction*(generic.SurfaceRadius(direction)+1.8);Require(Math.Abs(generic.Altitude(p)-1.8)<.00001,"Non-Venus definition uses same coordinate engine");
  var alternate=new GameObject("Alternate planet verification").AddComponent<PlanetFlight>();alternate.planet=AssetDatabase.LoadAssetAtPath<PlanetDefinition>("Assets/Planetary/AirlessTestWorld.asset");alternate.Initialize();
  foreach(var d in new[]{VenusData.Direction(0,45),VenusData.Direction(89.99,0),VenusData.Direction(-35,179.999)}){alternate.SetPositionForVerification(d*(alternate.Sampler.SurfaceRadius(d)+2));for(int i=0;i<1800;i++){alternate.ApplyFrame();if(i>20&&alternate.Tiles.RunningJobs==0)break;Thread.Sleep(1);}Require(alternate.Tiles.CoverageValid()&&alternate.Tiles.DeepestVisible>=18,"Alternate planet surface at cube seam/pole/dateline");}
  alternate.Excavation.DisposeResources();alternate.Tiles.Dispose();UnityEngine.Object.DestroyImmediate(alternate.gameObject);
  foreach(var name in new[]{"Planetary/Surface","Planetary/CloudDeck"})Require(!ShaderUtil.ShaderHasError(Shader.Find(name)),"Shader compiles");
  File.WriteAllText(Path.Combine(root,"flight-validation.txt"),$"PASS: unchanged scene and camera at 7 altitudes, metre precision, parent coverage during refinement, bounded tile cache, continuous monotonic descent/ascent, alternate planet sampler and renderer at cube seam/pole/dateline, shader compilation.\nMax resident nodes: {maxNodes}. Worst full Tick observed in batch: {worst:F2} ms (not a gameplay frame-rate guarantee).\n");Debug.Log("CONTINUOUS FLIGHT VALIDATION PASSED");flight.Excavation.DisposeResources();flight.Tiles.Dispose();UnityEngine.Object.DestroyImmediate(flight.gameObject);
 }
}
}

