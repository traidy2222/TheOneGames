using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEngine.Rendering;
using Excavation;
namespace VenusWorld {
public sealed class VenusExplorer:MonoBehaviour {
 public Material basalt,clouds,haze,relief;
 public double latitude=-7.5,longitude=303;
 public VenusSite Site{get;private set;}public DigTerrain Ground{get;private set;}
 public bool OnSurface{get;private set;}public Camera ActiveCamera=>OnSurface?surfaceCamera:orbitCamera;
 GameObject orbitRoot,surfaceRoot,rig;Camera surfaceCamera,orbitCamera;CharacterController body;Light sun;
 float yaw,pitch=12,fall,radius=1.8f,nextCut,orbitYaw=-57,orbitPitch=-7.5f,distance=18500;bool built,fly,study;
 readonly List<Mesh> meshes=new();readonly List<(GameObject go,Vector3 basePoint)> rocks=new();int lastEdit;
 string status="Press 1 to visit the surface near Venera 13.";
 string SavePath=>Path.Combine(Application.persistentDataPath,$"venus-{latitude:F3}-{longitude:F3}.bin");
 void Start(){Initialize();}
 public void Initialize(){if(built)return;built=true;VenusData.Load();Site=new VenusSite(latitude,longitude);orbitRoot=new GameObject("Venus · kilometres at orbital scale");orbitRoot.transform.SetParent(transform,false);BuildOrbit();var lightGo=new GameObject("Diffuse Venus daylight");sun=lightGo.AddComponent<Light>();sun.type=LightType.Directional;sun.color=new Color(1,.79f,.52f);sun.intensity=.95f;sun.shadows=LightShadows.Soft;sun.shadowStrength=.35f;sun.shadowNormalBias=.05f;sun.shadowBias=.08f;sun.transform.rotation=Quaternion.Euler(55,-40,0);SetOrbit();}
 void BuildOrbit(){
  var mesh=new Mesh{indexFormat=IndexFormat.UInt32};var v=new List<Vector3>();var normal=new List<Vector3>();var colors=new List<Color>();var ix=new List<int>();const int lat=256,lon=512;
  for(int y=0;y<=lat;y++)for(int x=0;x<=lon;x++){double latitude=90-y*180.0/lat,longitude=-180+x*360.0/lon;var d=VenusData.Direction(latitude,longitude);double h=VenusData.Elevation(latitude,longitude,out bool valid);v.Add(d.Float*(float)((VenusData.DatumRadius+h)/1000));normal.Add(d.Float);float shade=Mathf.InverseLerp(-2951,11687,(float)h);colors.Add(valid?Color.Lerp(new Color(.12f,.10f,.08f),new Color(.65f,.51f,.32f),shade):new Color(.35f,.1f,.35f));}
  for(int y=0;y<lat;y++)for(int x=0;x<lon;x++){int i=y*(lon+1)+x;ix.AddRange(new[]{i,i+1,i+lon+1,i+1,i+lon+2,i+lon+1});}
  mesh.SetVertices(v);mesh.SetNormals(normal);mesh.SetColors(colors);mesh.SetTriangles(ix,0);mesh.RecalculateBounds();meshes.Add(mesh);var ground=Render("Magellan global relief · no vertical exaggeration",mesh,relief,orbitRoot.transform);
  var cloudMesh=Instantiate(mesh);var cv=cloudMesh.vertices;for(int i=0;i<cv.Length;i++)cv[i]=normal[i]*(float)((VenusData.MeanRadius+65000)/1000);cloudMesh.vertices=cv;cloudMesh.RecalculateBounds();meshes.Add(cloudMesh);Render("Opaque cloud deck · artistic reconstruction",cloudMesh,clouds,orbitRoot.transform);
  var cameraGo=new GameObject("Orbital camera");cameraGo.transform.SetParent(orbitRoot.transform);orbitCamera=cameraGo.AddComponent<Camera>();orbitCamera.tag="MainCamera";orbitCamera.nearClipPlane=5;orbitCamera.farClipPlane=100000;orbitCamera.fieldOfView=45;orbitCamera.clearFlags=CameraClearFlags.SolidColor;orbitCamera.backgroundColor=new Color(.001f,.001f,.002f);orbitCamera.allowHDR=true;UpdateOrbit();
 }
 GameObject Render(string name,Mesh m,Material mat,Transform parent){var go=new GameObject(name);go.transform.SetParent(parent,false);go.AddComponent<MeshFilter>().sharedMesh=m;go.AddComponent<MeshRenderer>().sharedMaterial=mat;return go;}
 void BuildSurface(){
  surfaceRoot=new GameObject("Venera region · local metre frame");surfaceRoot.transform.SetParent(transform,false);
  var terrainGo=new GameObject("Diggable basalt patch · 72 m");terrainGo.transform.SetParent(surfaceRoot.transform,false);Ground=terrainGo.AddComponent<DigTerrain>();Ground.cellsX=Ground.cellsZ=96;Ground.cellsY=64;Ground.spacing=.75f;Ground.Seed=137;Ground.material=basalt;Ground.heightFunction=Site.Height;Ground.Initialize();if(File.Exists(SavePath)){try{Ground.Load(SavePath);Ground.Flush();}catch(Exception e){Debug.LogWarning(e.Message);}}
  var v=new List<Vector3>();var ix=new List<int>();const int n=160;const float step=16;
  for(int z=0;z<=n;z++)for(int x=0;x<=n;x++){float px=(x-n/2)*step,pz=(z-n/2)*step;v.Add(new Vector3(px,Site.Height(px,pz),pz));}
  for(int z=0;z<n;z++)for(int x=0;x<n;x++){float px=(x-n/2)*step,pz=(z-n/2)*step;if(px>=-32&&px<32&&pz>=-32&&pz<32)continue;int i=z*(n+1)+x;ix.AddRange(new[]{i,i+n+1,i+1,i+1,i+n+1,i+n+2});}
  var mesh=new Mesh{indexFormat=IndexFormat.UInt32};mesh.SetVertices(v);mesh.SetTriangles(ix,0);mesh.RecalculateNormals();mesh.RecalculateBounds();meshes.Add(mesh);var plain=Render("Regional lava plain · measured macro relief",mesh,basalt,surfaceRoot.transform);plain.AddComponent<MeshCollider>().sharedMesh=mesh;
  ScatterRocks();
  rig=new GameObject("Surface observer");rig.transform.SetParent(surfaceRoot.transform);body=rig.AddComponent<CharacterController>();body.height=1.8f;body.radius=.32f;body.center=new Vector3(0,.9f,0);body.stepOffset=.35f;
  var cam=new GameObject("Surface camera");cam.transform.SetParent(rig.transform);cam.transform.localPosition=new Vector3(0,1.65f,0);surfaceCamera=cam.AddComponent<Camera>();surfaceCamera.tag="MainCamera";surfaceCamera.nearClipPlane=.05f;surfaceCamera.farClipPlane=2200;surfaceCamera.fieldOfView=65;surfaceCamera.clearFlags=CameraClearFlags.Skybox;Respawn();
 }
 void ScatterRocks(){var rng=new System.Random(691);var mesh=MakeSlab();meshes.Add(mesh);for(int i=0;i<1000;i++){float x=((float)rng.NextDouble()-.5f)*220,z=((float)rng.NextDouble()-.5f)*220;var p=new Vector3(x,Site.Height(x,z)-.035f,z);var go=Render("Basalt plate",mesh,basalt,surfaceRoot.transform);go.transform.position=p;go.transform.rotation=Quaternion.Euler((float)rng.NextDouble()*14-7,(float)rng.NextDouble()*360,(float)rng.NextDouble()*14-7);float size=.18f+(float)Math.Pow(rng.NextDouble(),3)*1.4f;go.transform.localScale=new Vector3(size,.08f+size*.18f,size*(.6f+(float)rng.NextDouble()));rocks.Add((go,p));}}
 Mesh MakeSlab(){var v=new List<Vector3>();var ix=new List<int>();for(int ring=0;ring<3;ring++)for(int i=0;i<9;i++){float a=i*Mathf.PI*2/9;float radius=(ring==1?1:.8f)*(1+.15f*Mathf.Sin(i*17));v.Add(new Vector3(Mathf.Cos(a)*radius,ring*.45f,Mathf.Sin(a)*radius));}for(int r=0;r<2;r++)for(int i=0;i<9;i++){int j=(i+1)%9,a=r*9+i,b=r*9+j;ix.AddRange(new[]{a,b,a+9,b,b+9,a+9});}v.Add(new Vector3(0,.9f,0));for(int i=0;i<9;i++)ix.AddRange(new[]{27,18+(i+1)%9,18+i});var m=new Mesh();m.SetVertices(v);m.SetTriangles(ix,0);m.RecalculateNormals();m.RecalculateBounds();return m;}
 public void SetSurface(){Initialize();if(!surfaceRoot)BuildSurface();orbitRoot.SetActive(false);surfaceRoot.SetActive(true);OnSurface=true;RenderSettings.fog=true;RenderSettings.fogMode=FogMode.ExponentialSquared;RenderSettings.fogColor=new Color(.48f,.33f,.16f);RenderSettings.fogDensity=.0016f;RenderSettings.skybox=haze;RenderSettings.ambientMode=AmbientMode.Trilight;RenderSettings.ambientSkyColor=new Color(.56f,.43f,.28f);RenderSettings.ambientEquatorColor=new Color(.39f,.30f,.20f);RenderSettings.ambientGroundColor=new Color(.12f,.095f,.07f);sun.enabled=true;Physics.SyncTransforms();status="Metre-scale reconstruction near the Venera 13 landing region.";}
 public void SetOrbit(){if(!built){Initialize();return;}if(surfaceRoot){if(Ground)Ground.Save(SavePath);surfaceRoot.SetActive(false);}orbitRoot.SetActive(true);OnSurface=false;RenderSettings.fog=false;RenderSettings.skybox=null;sun.enabled=false;RenderSettings.ambientLight=Color.black;UpdateOrbit();}
 void Respawn(){body.enabled=false;rig.transform.position=new Vector3(0,Site.Height(0,-10)+1,-10);body.enabled=true;fall=0;}
 void UpdateOrbit(){var d=VenusData.Direction(orbitPitch,orbitYaw);orbitCamera.transform.position=d.Float*distance;orbitCamera.transform.LookAt(Vector3.zero);}
 void Update(){
  if(Input.GetKeyDown(KeyCode.Alpha1))SetSurface();if(Input.GetKeyDown(KeyCode.Alpha2))SetOrbit();
  if(Input.GetKeyDown(KeyCode.Escape)){Cursor.lockState=CursorLockMode.None;Cursor.visible=true;}
  if(Cursor.lockState!=CursorLockMode.Locked){if(Input.GetMouseButtonDown(0)){Cursor.lockState=CursorLockMode.Locked;Cursor.visible=false;}return;}
  if(!OnSurface){orbitYaw+=Input.GetAxisRaw("Mouse X")*.6f;orbitPitch=Mathf.Clamp(orbitPitch-Input.GetAxisRaw("Mouse Y")*.6f,-85,85);distance=Mathf.Clamp(distance-Input.mouseScrollDelta.y*500,8000,40000);if(Input.GetKeyDown(KeyCode.G)){study=!study;orbitRoot.transform.Find("Opaque cloud deck · artistic reconstruction").gameObject.SetActive(!study);}UpdateOrbit();return;}
  float dt=Mathf.Min(Time.deltaTime,.05f);yaw+=Input.GetAxisRaw("Mouse X")*1.6f;pitch=Mathf.Clamp(pitch-Input.GetAxisRaw("Mouse Y")*1.6f,-85,85);rig.transform.rotation=Quaternion.Euler(0,yaw,0);surfaceCamera.transform.localRotation=Quaternion.Euler(pitch,0,0);
  if(Input.GetKeyDown(KeyCode.F)){fly=!fly;fall=0;}Vector3 motion=Vector3.ClampMagnitude(rig.transform.right*Input.GetAxisRaw("Horizontal")+rig.transform.forward*Input.GetAxisRaw("Vertical"),1)*(Input.GetKey(KeyCode.LeftShift)?7:3);
  if(fly)motion.y=((Input.GetKey(KeyCode.Space)?1:0)-(Input.GetKey(KeyCode.LeftControl)?1:0))*5;else {if(body.isGrounded&&fall<0)fall=-2;if(body.isGrounded&&Input.GetKeyDown(KeyCode.Space))fall=4.5f;fall-=VenusData.Gravity*dt;motion.y=fall;}body.Move(motion*dt);
  if(rig.transform.position.y< -20||new Vector2(rig.transform.position.x,rig.transform.position.z).magnitude>1000||Input.GetKeyDown(KeyCode.Home))Respawn();radius=Mathf.Clamp(radius+Input.mouseScrollDelta.y*.15f,.9f,4);
  if(Input.GetKeyDown(KeyCode.F5)){Ground.Save(SavePath);status="Local excavation saved";}if(Input.GetKeyDown(KeyCode.F9)){try{Ground.Load(SavePath);status="Local excavation restored";}catch(Exception e){status=e.Message;}}
  if(Input.GetKeyDown(KeyCode.Z))Ground.Undo();
  if(Physics.Raycast(surfaceCamera.ViewportPointToRay(new Vector3(.5f,.5f,0)),out var hit,12)&&hit.collider.GetComponentInParent<DigTerrain>()==Ground){
   bool fill=Input.GetMouseButton(1),dig=Input.GetMouseButton(0);if(Input.GetMouseButtonDown(0)||Input.GetMouseButtonDown(1))Ground.BeginStroke();
   if((fill||dig)&&Time.time>nextCut&&Ground.PendingChunks==0){if(Mathf.Abs(hit.point.x)>30||Mathf.Abs(hit.point.z)>30){status="Beyond the current excavation patch; regional terrain remains explorable.";return;}
    if(fill){if(Vector3.Distance(hit.point,rig.transform.position)>radius+1)Ground.Deposit(hit.point,radius);}else if(Input.GetKey(KeyCode.LeftAlt))Ground.Smooth(hit.point,radius);else Ground.Sculpt(hit.point+hit.normal*(radius-.25f),radius,false);nextCut=Time.time+.12f;}
  }
  if(Ground.EditCount!=lastEdit){foreach(var item in rocks)if(Mathf.Abs(item.basePoint.x)<32&&Mathf.Abs(item.basePoint.z)<32)item.go.SetActive(Ground.Sample(item.basePoint-Vector3.up*.15f)<0&&Ground.Sample(item.basePoint+Vector3.up*.5f)>0);lastEdit=Ground.EditCount;}
 }
 void OnGUI(){float s=Mathf.Clamp(Screen.width/1600f,.75f,1.3f);GUI.matrix=Matrix4x4.Scale(Vector3.one*s);float w=Screen.width/s,h=Screen.height/s;GUI.color=new Color(.035f,.028f,.02f,.86f);GUI.DrawTexture(new Rect(24,24,440,142),Texture2D.whiteTexture);GUI.DrawTexture(new Rect(24,h-113,780,88),Texture2D.whiteTexture);GUI.color=Color.white;var title=new GUIStyle(GUI.skin.label){fontSize=27,fontStyle=FontStyle.Bold};var small=new GUIStyle(GUI.skin.label){fontSize=15};GUI.Label(new Rect(42,34,420,40),"VENUS  /  MAGELLAN",title);GUI.Label(new Rect(42,81,415,72),OnSurface?$"Venera 13 region · 7.5° S / 303° E\nHuman scale · gravity 8.87 m/s²\nReference environment ≈464°C · ≈92 bar":$"Mean radius 6,051.8 km · true-scale relief\n{(study?"Elevation study · purple = missing data":"Cloud-covered orbital view")}\nMagellan / USGS elevation data",small);GUI.Label(new Rect(42,h-104,750,75),OnSurface?"WASD walk · Shift faster · Space jump · F survey flight · Home return\nLMB excavate · RMB deposit · Alt+LMB grade · wheel brush · Z undo · F5/F9 save/load\n2 orbit · Esc cursor · 72 m editable patch; local rock detail is reconstructed":"Mouse orbit · wheel zoom · G reveal measured relief\n1 visit Venera 13 region · Esc release cursor\nCloud appearance is reconstructed; measured elevations are not exaggerated.",small);GUI.Label(new Rect(w-550,h-56,520,40),status,small);if(OnSurface){GUI.DrawTexture(new Rect(w/2-5,h/2,10,1),Texture2D.whiteTexture);GUI.DrawTexture(new Rect(w/2,h/2-5,1,10),Texture2D.whiteTexture);}if(Cursor.lockState!=CursorLockMode.Locked)GUI.Label(new Rect(w/2-135,h/2+28,350,40),"Click to take control",title);}
 void OnDisable(){Cursor.lockState=CursorLockMode.None;Cursor.visible=true;}
 void OnDestroy(){foreach(var mesh in meshes)if(mesh){if(Application.isPlaying)Destroy(mesh);else DestroyImmediate(mesh);}}
}
}

