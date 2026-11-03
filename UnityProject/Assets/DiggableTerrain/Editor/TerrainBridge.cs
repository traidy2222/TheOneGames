using System;
using System.IO;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;
namespace Excavation.EditorTools {
// Local filesystem command bridge. Only explicitly allowlisted development actions run.
[InitializeOnLoad]
public static class TerrainBridge {
    static double next;
    static readonly string Root=Path.GetFullPath(Path.Combine(Application.dataPath,"../Library/TerrainBridge"));
    [Serializable] public class Command { public string action; }
    static TerrainBridge(){Directory.CreateDirectory(Root);EditorApplication.update+=Poll;}
    static void Poll(){
        if(EditorApplication.timeSinceStartup<next||EditorApplication.isCompiling||EditorApplication.isUpdating)return;
        next=EditorApplication.timeSinceStartup+.5;
        string input=Path.Combine(Root,"request.json");if(!File.Exists(input))return;
        try{var cmd=JsonUtility.FromJson<Command>(File.ReadAllText(input));File.Delete(input);
            switch(cmd.action){
                case "flight": if(EditorApplication.isPlaying)throw new Exception("Stop Play mode first.");UnityEditor.SceneManagement.EditorSceneManager.OpenScene("Assets/Scenes/PlanetaryFlight.unity");break;
                case "flight-descend": Flight().BeginDescent();break;
                case "flight-ascend": Flight().BeginAscent();break;
                case "flight-status": break;
                case "lighting-verify": Planetary.EditorTools.TunnelLightingValidation.StartLive();break;
                case "volume-verify": Planetary.EditorTools.PlanetVolumeValidation.StartLive();break;
                case "flight-verify": Planetary.EditorTools.PlanetFlightSetup.StartLiveVerification();break;
                case "flight-validate-ground": Planetary.EditorTools.PlanetFlightSetup.ValidateGround(Flight());break;
                case "status": break; case "venus": if(EditorApplication.isPlaying)throw new Exception("Stop Play mode first.");UnityEditor.SceneManagement.EditorSceneManager.OpenScene("Assets/Scenes/Venus.unity");break; case "venus-surface": UnityEngine.Object.FindFirstObjectByType<VenusWorld.VenusExplorer>().SetSurface();break; case "venus-orbit": UnityEngine.Object.FindFirstObjectByType<VenusWorld.VenusExplorer>().SetOrbit();break;
                case "planet": if(EditorApplication.isPlaying)throw new Exception("Stop Play mode before changing scenes.");UnityEditor.SceneManagement.EditorSceneManager.OpenScene("Assets/Scenes/Planet.unity");break;
                case "landscape": if(EditorApplication.isPlaying)throw new Exception("Stop Play mode before changing scenes.");UnityEditor.SceneManagement.EditorSceneManager.OpenScene("Assets/Scenes/Excavation.unity");break;
                case "validate-planet": if(EditorApplication.isPlaying)throw new Exception("Stop Play mode before validation.");PlanetSetup.Validate();break;
                case "refresh": AssetDatabase.Refresh();break;
                case "setup": TerrainSetup.Create();break;
                case "save": var currentFlight=UnityEngine.Object.FindFirstObjectByType<Planetary.PlanetFlight>();if(currentFlight&&currentFlight.Initialized)currentFlight.Excavation.Save();else UnityEngine.Object.FindFirstObjectByType<DigTerrain>().Save(Path.Combine(Root,"terrain-backup.bin"));break;
                case "validate": if(EditorApplication.isPlaying)throw new Exception("Run validation outside Play mode.");TerrainSetup.Validate();break;
                case "play": PlayerSettings.runInBackground=true;EditorApplication.isPaused=false;EditorApplication.isPlaying=true;break;
                case "stop": EditorApplication.isPlaying=false;break;
                case "build": Build();break;
                case "capture": Capture();break;
                default: throw new InvalidOperationException("Unknown action");
            }
            File.WriteAllText(Path.Combine(Root,"response.json"),JsonUtility.ToJson(new Result{ok=true,action=cmd.action,message=cmd.action=="flight-status"?FlightStatus():"Command completed",playing=EditorApplication.isPlaying,scene=UnityEngine.SceneManagement.SceneManager.GetActiveScene().path}));
        }catch(Exception e){File.WriteAllText(Path.Combine(Root,"response.json"),JsonUtility.ToJson(new Result{ok=false,message=e.ToString()}));}
    }
    static Planetary.PlanetFlight Flight(){if(!EditorApplication.isPlaying)throw new Exception("Start Play mode first");var f=UnityEngine.Object.FindFirstObjectByType<Planetary.PlanetFlight>();if(!f)throw new Exception("Open the PlanetaryFlight scene first");return f;}
    static string FlightStatus(){var f=Flight();return $"frame={Time.frameCount}; paused={EditorApplication.isPaused}; altitude={f.Altitude:F3}; scale={f.MetresPerRenderUnit:F3}; camera={f.View.GetInstanceID()}; scene={f.gameObject.scene.handle}; nodes={f.Tiles.ResidentCount}; visible={f.Tiles.VisibleCount}; level={f.Tiles.DeepestVisible}; jobs={f.Tiles.RunningJobs}; coverage={f.Tiles.CoverageValid()}; excavationReady={f.Excavation.Ready}; volumeNodes={f.Excavation.Volume.ResidentCount}; volumePages={f.Excavation.Store.CachedPages}; viewDistance={f.Excavation.Volume.ViewDistance}; visibleVolume={f.Excavation.Volume.VisibleCount}; volumeJobs={f.Excavation.Volume.Pending}; cruise={f.CruiseTarget}; travelled={f.TravelledMetres:F1}";}
    [Serializable] class Result {public bool ok;public string action;public string message;public bool playing;public string scene;}
    public static void BatchVerify(){
        TerrainSetup.Create();TerrainSetup.Validate();
        var terrain=UnityEngine.Object.FindFirstObjectByType<DigTerrain>();terrain.Initialize();
        var cam=Camera.main;cam.transform.position=new Vector3(18,23,-24);cam.transform.LookAt(new Vector3(0,8,2));
        terrain.Sculpt(new Vector3(0,terrain.Surface(0,0)-1,0),4.5f,false);terrain.Flush();
        terrain.Deposit(new Vector3(-6,terrain.Surface(-6,1),1),3);terrain.Flush();
        UnityEngine.Object.FindFirstObjectByType<LandscapeEnvironment>().Build();Capture();
        File.Copy(Path.Combine(Root,"preview.png"),Path.Combine(Root,"overview.png"),true);
        cam.transform.position=new Vector3(2,terrain.Surface(2,-7)+2,-7);cam.transform.LookAt(new Vector3(0,terrain.Surface(0,0)-2,0));Capture();
        File.Copy(Path.Combine(Root,"preview.png"),Path.Combine(Root,"cutaway.png"),true);
        Debug.Log("BATCH TERRAIN AND RENDER CHECK PASSED");
    }
    public static void Build(){
        var report=BuildPipeline.BuildPlayer(Array.ConvertAll(Array.FindAll(EditorBuildSettings.scenes,s=>s.enabled),s=>s.path),"Builds/Venus/Venus.exe",BuildTarget.StandaloneWindows64,BuildOptions.None);
        if(report.summary.result!=BuildResult.Succeeded)throw new Exception("Build failed: "+report.summary.result);
    }
    public static void Capture(){
        var cam=Camera.main;if(!cam)throw new Exception("No main camera");
        var old=cam.targetTexture;var active=RenderTexture.active;var rt=new RenderTexture(1280,720,24);var tex=new Texture2D(1280,720,TextureFormat.RGB24,false);
        try{cam.targetTexture=rt;cam.Render();RenderTexture.active=rt;tex.ReadPixels(new Rect(0,0,1280,720),0,0);tex.Apply();File.WriteAllBytes(Path.Combine(Root,"preview.png"),tex.EncodeToPNG());}
        finally{cam.targetTexture=old;RenderTexture.active=active;UnityEngine.Object.DestroyImmediate(tex);rt.Release();UnityEngine.Object.DestroyImmediate(rt);}
    }
}
}




