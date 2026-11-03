using System;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
namespace Excavation.EditorTools {
[InitializeOnLoad]
public static class TerrainSetup {
    const string Request="terrain-setup.request";
    static TerrainSetup(){EditorApplication.delayCall+=Check;}
    static void Check(){if(!File.Exists(Request))return;File.Delete(Request);try{Create();Validate();File.WriteAllText("terrain-validation.txt","PASS: scene generation, density excavation/refill, collider excavation/refill, chunk-boundary edit, undo, save/load round trip, malformed save rejection, closed cavity and mesh checks.");}catch(Exception e){File.WriteAllText("terrain-validation.txt",e.ToString());Debug.LogException(e);}}
    [MenuItem("Excavation/Create demo scene")]
    public static void Create(){
        if(EditorApplication.isPlaying)throw new InvalidOperationException("Leave play mode first.");
        if(!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo())throw new OperationCanceledException();
        var scene=EditorSceneManager.NewScene(NewSceneSetup.EmptyScene,NewSceneMode.Single);
        RenderSettings.ambientMode=UnityEngine.Rendering.AmbientMode.Trilight;RenderSettings.ambientSkyColor=new Color(.38f,.46f,.56f);RenderSettings.ambientEquatorColor=new Color(.26f,.29f,.24f);RenderSettings.ambientGroundColor=new Color(.12f,.10f,.08f);
        RenderSettings.fog=true;RenderSettings.fogColor=new Color(.59f,.70f,.77f);RenderSettings.fogMode=FogMode.ExponentialSquared;RenderSettings.fogDensity=.0025f;
        var sun=new GameObject("Late afternoon sun").AddComponent<Light>();sun.type=LightType.Directional;sun.intensity=1.05f;sun.color=new Color(1,.91f,.76f);sun.shadows=LightShadows.Soft;sun.shadowNormalBias=0;sun.shadowBias=.025f;sun.transform.rotation=Quaternion.Euler(32,-35,0);
        var sky=new Material(Shader.Find("Skybox/Procedural"));sky.SetFloat("_AtmosphereThickness",1.1f);sky.SetFloat("_Exposure",1.1f);sky.SetColor("_SkyTint",new Color(.5f,.52f,.55f));RenderSettings.skybox=sky;RenderSettings.sun=sun;
        if(AssetDatabase.LoadAssetAtPath<Material>("Assets/DiggableTerrain/Sky.mat"))AssetDatabase.DeleteAsset("Assets/DiggableTerrain/Sky.mat");AssetDatabase.CreateAsset(sky,"Assets/DiggableTerrain/Sky.mat");
        QualitySettings.shadowDistance=140;QualitySettings.shadows=ShadowQuality.All;QualitySettings.shadowResolution=ShadowResolution.High;QualitySettings.antiAliasing=4;
        var terrain=new GameObject("Diggable landscape").AddComponent<DigTerrain>();
        string matPath="Assets/DiggableTerrain/Ground.mat";var mat=AssetDatabase.LoadAssetAtPath<Material>(matPath);if(!mat){mat=new Material(Shader.Find("Excavation/Strata"));AssetDatabase.CreateAsset(mat,matPath);}terrain.material=mat;terrain.cellsX=144;terrain.cellsY=72;terrain.cellsZ=144;terrain.spacing=.5f;
        string[] assets={"grass_ground","brown_mud_dry","rock_boulder_dry"};string[] slots={"_Grass","_Soil","_Rock"};
        for(int i=0;i<3;i++)for(int k=0;k<3;k++){string suffix=k==0?"Albedo":k==1?"Normal":"Rough";string path="Assets/DiggableTerrain/Textures/"+assets[i]+"_"+suffix+".jpg";var importer=(TextureImporter)AssetImporter.GetAtPath(path);importer.sRGBTexture=k==0;importer.wrapMode=TextureWrapMode.Repeat;importer.anisoLevel=8;importer.mipmapEnabled=true;importer.SaveAndReimport();mat.SetTexture(slots[i]+(k==0?"":k==1?"N":"R"),AssetDatabase.LoadAssetAtPath<Texture2D>(path));}
        EditorUtility.SetDirty(mat);
        string grassPath="Assets/DiggableTerrain/Meadow.mat";var grass=AssetDatabase.LoadAssetAtPath<Material>(grassPath);if(!grass){grass=new Material(Shader.Find("Excavation/Meadow"));AssetDatabase.CreateAsset(grass,grassPath);}var env=new GameObject("Highland landscape").AddComponent<LandscapeEnvironment>();env.terrain=terrain;env.grassMaterial=grass;
        var player=new GameObject("Surveyor");var cc=player.AddComponent<CharacterController>();cc.height=1.8f;cc.radius=.32f;cc.center=new Vector3(0,.9f,0);cc.stepOffset=.4f;
        var cam=new GameObject("Main Camera").AddComponent<Camera>();cam.tag="MainCamera";cam.transform.SetParent(player.transform);cam.transform.localPosition=new Vector3(0,1.65f,0);cam.backgroundColor=RenderSettings.fogColor;cam.clearFlags=CameraClearFlags.Skybox;cam.farClipPlane=950;cam.fieldOfView=70;cam.gameObject.AddComponent<AudioListener>();
        var controller=player.AddComponent<DigPlayer>();controller.terrain=terrain;controller.view=cam;player.transform.position=new Vector3(0,12,-13);
        var lamp=cam.gameObject.AddComponent<Light>();lamp.type=LightType.Point;lamp.range=16;lamp.intensity=0;lamp.color=new Color(1,.86f,.65f);
        Directory.CreateDirectory("Assets/Scenes");EditorSceneManager.SaveScene(scene,"Assets/Scenes/Excavation.unity");EditorBuildSettings.scenes=new[]{new EditorBuildSettingsScene("Assets/Scenes/Excavation.unity",true)};AssetDatabase.SaveAssets();
    }
    [MenuItem("Excavation/Validate terrain")]
    public static void Validate(){
        var go=new GameObject("Validation terrain");var t=go.AddComponent<DigTerrain>();t.cellsX=32;t.cellsY=32;t.cellsZ=32;t.spacing=.75f;
        string path=Path.Combine(Application.temporaryCachePath,"terrain-validation.bin");
        try {
            t.Initialize();Vector3 at=new Vector3(0,0,0);float before=t.Sample(at);Require(before<0,"Initially solid");
            t.BeginStroke();t.Sculpt(at,2,false);t.Flush();Require(t.Sample(at)>1,"Underground cavity");Require(t.Sample(at+Vector3.up*4)<0,"Cavity roof remains solid");
            Require(Physics.Raycast(at,Vector3.up,out var roof,5)&&roof.collider.transform.IsChildOf(go.transform),"Collision inside cavity");
            t.Undo();t.Flush();Require(Mathf.Abs(t.Sample(at)-before)<.001f,"Undo restores density");
            var top=new Vector3(0,20,0);Require(Physics.Raycast(top,Vector3.down,out var first,40),"Surface collision");
            t.BeginStroke();t.Sculpt(first.point,3,false);t.Flush();Require(Physics.Raycast(top,Vector3.down,out var dug,40)&&dug.point.y<first.point.y-1,"Collision moves after excavation");
            float dugDensity=t.Sample(first.point);t.Save(path);t.Sculpt(first.point,3,true);t.Flush();Require(Physics.Raycast(top,Vector3.down,out var filled,40)&&filled.point.y>dug.point.y+1,"Fill restores collision");
            t.Load(path);t.Flush();Require(Mathf.Abs(t.Sample(first.point)-dugDensity)<.001f,"Save round trip");
            File.WriteAllText(path,"broken");bool rejected=false;try{t.Load(path);}catch(InvalidDataException){rejected=true;}catch(EndOfStreamException){rejected=true;}Require(rejected,"Reject malformed save");Require(Mathf.Abs(t.Sample(first.point)-dugDensity)<.001f,"Failed load is atomic");
            // x=z=0 lies at the four-way chunk boundary in this test volume.
            Require(t.ChunkCount==8&&t.PendingChunks==0,"Chunk boundary rebuilds complete");
            foreach(var f in go.GetComponentsInChildren<MeshFilter>()){var mesh=f.sharedMesh;Require(mesh!=null,"Mesh exists");foreach(var n in mesh.normals)Require(float.IsFinite(n.x)&&float.IsFinite(n.y)&&float.IsFinite(n.z),"Finite normals");Require(mesh.triangles.Length%3==0,"Triangle topology");}
            Debug.Log("EXCAVATION VALIDATION PASSED");
        } finally {UnityEngine.Object.DestroyImmediate(go);if(File.Exists(path))File.Delete(path);}
    }
    static void Require(bool value,string label){if(!value)throw new Exception("Terrain validation failed: "+label);}
}
}



