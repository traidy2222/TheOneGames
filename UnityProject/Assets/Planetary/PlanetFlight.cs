using System;
using UnityEngine;
using VenusWorld;
namespace Planetary {
public sealed class PlanetFlight:MonoBehaviour {
 public PlanetDefinition planet;
 public bool VerificationInputLock{get;set;}
 public DVector Position {get;private set;}
 public double Altitude {get;private set;}
 public double MetresPerRenderUnit {get;private set;}=1;
 public PlanetTiles Tiles{get;private set;}
 public Camera View{get;private set;}
 public PlanetSampler Sampler{get;private set;}
 public int OriginShifts{get;private set;}
 public bool Initialized{get;private set;}
 public double TravelledMetres{get;private set;}
 public FlightExcavation Excavation{get;private set;} GameObject cloudShell;Material cloud;Light sun,headlamp;public Vector3 SunDirection=>-sun.transform.forward;public DVector HeadlampPosition=>Position+new DVector(headlamp.transform.position.x,headlamp.transform.position.y,headlamp.transform.position.z)*MetresPerRenderUnit;public bool HeadlampOn=>headlamp&&headlamp.enabled;public void SetHeadlamp(bool value){if(headlamp)headlamp.enabled=value;}Quaternion ascentView;int cruise;double speedFactor=1;string message="1 descend continuously · 2 return to space";
 public void Initialize(){if(Initialized)return;if(!planet)throw new InvalidOperationException("Select a planet definition");Application.runInBackground=true;Application.targetFrameRate=60;Initialized=true;Sampler=planet.CreateSampler();Position=VenusData.Direction(-7.5,303)*(planet.radiusMetres*3);
  var camera=new GameObject("Continuous observer");camera.transform.SetParent(transform);View=camera.AddComponent<Camera>();View.tag="MainCamera";View.fieldOfView=60;View.clearFlags=CameraClearFlags.SolidColor;View.transform.rotation=Quaternion.LookRotation(-Position.Normalized.Float,Vector3.up);View.allowHDR=true;View.renderingPath=RenderingPath.Forward;
  var root=new GameObject("Streamed planetary tiles");root.transform.SetParent(transform);Tiles=new PlanetTiles(Sampler,root.transform,planet.terrainMaterial);
  sun=new GameObject("Planet daylight").AddComponent<Light>();sun.transform.SetParent(transform);sun.type=LightType.Directional;sun.transform.rotation=Quaternion.LookRotation(-new Vector3(.6f,.25f,-.75f));sun.intensity=.95f;sun.color=Color.white;sun.shadows=LightShadows.Soft;sun.shadowBias=.03f;sun.shadowNormalBias=.1f;QualitySettings.shadows=ShadowQuality.All;QualitySettings.shadowDistance=512;QualitySettings.shadowCascades=4;QualitySettings.shadowResolution=ShadowResolution.High;QualitySettings.pixelLightCount=4;RenderSettings.sun=sun;
  headlamp=new GameObject("Shadowed headlamp").AddComponent<Light>();headlamp.transform.SetParent(View.transform,false);headlamp.transform.localPosition=new Vector3(.2f,-.1f,.15f);headlamp.type=LightType.Spot;headlamp.spotAngle=70;headlamp.innerSpotAngle=48;headlamp.range=80;headlamp.intensity=5;headlamp.color=new Color(1,.94f,.84f);headlamp.shadows=LightShadows.Soft;headlamp.shadowBias=.015f;headlamp.shadowNormalBias=.05f;headlamp.shadowNearPlane=.05f;headlamp.renderMode=LightRenderMode.ForcePixel;
  if(planet.cloudMaterial&&planet.cloudHeight>0){cloud=new Material(planet.cloudMaterial);cloudShell=new GameObject("Continuous cloud layer");cloudShell.transform.SetParent(transform);cloudShell.AddComponent<MeshFilter>().sharedMesh=Sphere();cloudShell.AddComponent<MeshRenderer>().sharedMaterial=cloud;}
  Excavation=gameObject.AddComponent<FlightExcavation>();Excavation.Bind(this);ApplyFrame();
 }
 void Start(){Initialize();}
 public void BeginDescent(){Initialize();if(Altitude<-.5){message="Underground: follow your tunnel with WASD";return;}cruise=-1;message="Descending · WASD cancels automatic flight";}
 public void BeginAscent(){Initialize();if(Altitude<-.5){message="Underground: follow your tunnel with WASD";return;}ascentView=View.transform.rotation;cruise=1;message="Ascending · WASD cancels automatic flight";}
 public void StopCruise(){cruise=0;}
 public void SetPositionForVerification(DVector p){Initialize();Position=p;ApplyFrame();}
 public void Simulate(float dt){Initialize();dt=Mathf.Clamp(dt,0,.05f);var before=Position;
  if(cruise!=0){double h=Sampler.Altitude(Position);double step=cruise<0?Math.Max(1,(h-2)*.32):Math.Max(5,h*.32);step=Math.Min(step,2e6)*dt;h=cruise<0?Math.Max(2,h-step):Math.Min(planet.radiusMetres*2,h+step);Position=Position.Normalized*(Sampler.SurfaceRadius(Position)+h);if(cruise<0&&h<300){var u=Position.Normalized.Float;var tangent=Vector3.ProjectOnPlane(Vector3.up,u).normalized;if(tangent.sqrMagnitude<.1f)tangent=Vector3.ProjectOnPlane(Vector3.forward,u).normalized;float blend=Mathf.SmoothStep(0,1,(float)((300-h)/298));View.transform.rotation=Quaternion.Slerp(Quaternion.LookRotation(-u,tangent),Quaternion.LookRotation(tangent-u*.15f,u),blend);}if(h<=2.001&&cruise<0){cruise=0;message="Surface reached · continue flying with WASD";}if(cruise>0){var u=Position.Normalized.Float;var tangent=Vector3.ProjectOnPlane(Vector3.up,u).normalized;if(tangent.sqrMagnitude<.1f)tangent=Vector3.ProjectOnPlane(Vector3.forward,u).normalized;View.transform.rotation=Quaternion.Slerp(ascentView,Quaternion.LookRotation(-u,tangent),Mathf.SmoothStep(0,1,(float)((h-300)/3000)));}if(h>=planet.radiusMetres*2&&cruise>0)cruise=0;}
  TravelledMetres+=(Position-before).Magnitude;ApplyFrame();
 }
 public void ApplyFrame(){if(!Initialized)return;Altitude=Sampler.Altitude(Position);double scale=Math.Max(1,Math.Max(0,Altitude)/10000);if(Math.Abs(scale-MetresPerRenderUnit)>.00001)OriginShifts++;MetresPerRenderUnit=scale;View.transform.position=Vector3.zero;View.nearClipPlane=(float)(Math.Max(.05,Math.Max(0,Altitude)*1e-5)/scale);View.farClipPlane=(float)((planet.radiusMetres*3+Math.Max(0,Altitude))/scale);
  double air=planet.atmosphereHeight>0?Math.Max(0,1-Math.Max(0,Altitude)/planet.atmosphereHeight):0;
  View.backgroundColor=Color.Lerp(new Color(.001f,.001f,.002f),planet.hazeColor,(float)(air*air));if(planet.cloudHeight>0){float cloudBand=(float)Math.Exp(-Math.Pow((Altitude-planet.cloudHeight)/Math.Max(1,planet.cloudBandWidth*2),2));View.backgroundColor=Color.Lerp(View.backgroundColor,planet.cloudHazeColor,cloudBand);}
  float underground=Mathf.SmoothStep(0,1,(float)(-Altitude/6));Shader.SetGlobalFloat("_Underground",underground);
  RenderSettings.skybox=null;RenderSettings.ambientMode=UnityEngine.Rendering.AmbientMode.Trilight;RenderSettings.ambientSkyColor=Color.Lerp(new Color(.07f,.07f,.08f),planet.ambientDaylight,(float)air);RenderSettings.ambientEquatorColor=RenderSettings.ambientSkyColor*.7f;RenderSettings.ambientGroundColor=RenderSettings.ambientSkyColor*.3f;
  RenderSettings.fog=air>0;RenderSettings.fogMode=FogMode.ExponentialSquared;RenderSettings.fogColor=View.backgroundColor;double fog=planet.atmosphereHeight>0?planet.groundFogDensity*Math.Exp(-Math.Max(0,Altitude)/Math.Max(1,planet.atmosphereScaleHeight)):0;if(planet.cloudHeight>0)fog+=.0008*Math.Exp(-Math.Pow((Altitude-planet.cloudHeight)/Math.Max(1,planet.cloudBandWidth),2));fog*=1-underground;RenderSettings.fogDensity=(float)(fog*scale);Shader.SetGlobalColor("_PlanetHaze",View.backgroundColor);Shader.SetGlobalFloat("_PlanetFog",(float)(fog*scale));Shader.SetGlobalColor("_PlanetAmbient",RenderSettings.ambientSkyColor);
  if(cloudShell){cloudShell.transform.position=(Position*(-1/scale)).Float;cloudShell.transform.localScale=Vector3.one*(float)((planet.radiusMetres+planet.cloudHeight)/scale);float opacity=Mathf.SmoothStep(0,1,(float)((Altitude-planet.cloudHeight-500)/10000));cloud.SetFloat("_Opacity",opacity);cloudShell.GetComponent<MeshRenderer>().enabled=opacity>.0001;}
  Tiles.Tick(Position,scale);
 }
 void Update(){Initialize();if(VerificationInputLock||!Application.isFocused){Simulate(Time.deltaTime);return;}if(Input.GetKeyDown(KeyCode.Escape)){Cursor.lockState=CursorLockMode.None;Cursor.visible=true;cruise=0;}if(Input.GetKeyDown(KeyCode.L))SetHeadlamp(!HeadlampOn);if(Input.GetKeyDown(KeyCode.Alpha1))BeginDescent();if(Input.GetKeyDown(KeyCode.Alpha2))BeginAscent();
  if(Cursor.lockState!=CursorLockMode.Locked){if(Input.GetMouseButtonDown(0)){Cursor.lockState=CursorLockMode.Locked;Cursor.visible=false;}Simulate(Time.deltaTime);return;}
  Vector3 up=Position.Magnitude>10?Position.Normalized.Float:View.transform.up;View.transform.rotation=Quaternion.AngleAxis(Input.GetAxisRaw("Mouse X")*1.5f,up)*View.transform.rotation;View.transform.rotation=Quaternion.AngleAxis(-Input.GetAxisRaw("Mouse Y")*1.5f,View.transform.right)*View.transform.rotation;
  Vector3 move=View.transform.forward*Input.GetAxisRaw("Vertical")+View.transform.right*Input.GetAxisRaw("Horizontal")+up*((Input.GetKey(KeyCode.Space)?1:0)-(Input.GetKey(KeyCode.LeftControl)?1:0));
  speedFactor=Math.Max(.05,Math.Min(20,speedFactor*Math.Pow(1.25,Input.mouseScrollDelta.y)));double speed=Math.Max(3,Math.Min(2e6,Math.Max(0,Altitude)*.45))*speedFactor*(Input.GetKey(KeyCode.LeftShift)?3:1);if(move.sqrMagnitude>.01){cruise=0;move=Vector3.ClampMagnitude(move,1);var desired=Position+new DVector(move.x,move.y,move.z)*(Math.Min(speed,Altitude<10?12:double.MaxValue)*Math.Min(Time.deltaTime,.05));Position=Excavation.ResolveMovement(Position,desired);}
  Simulate(Time.deltaTime);
 }
 public double CruiseTarget=>cruise;
 void OnGUI(){if(!Initialized)return;float s=Mathf.Clamp(Screen.width/1600f,.75f,1.3f);GUI.matrix=Matrix4x4.Scale(Vector3.one*s);float w=Screen.width/s,h=Screen.height/s;GUI.color=new Color(.025f,.025f,.03f,.85f);GUI.DrawTexture(new Rect(24,24,480,132),Texture2D.whiteTexture);GUI.DrawTexture(new Rect(24,h-112,820,88),Texture2D.whiteTexture);GUI.color=Color.white;var title=new GUIStyle(GUI.skin.label){fontSize=25,fontStyle=FontStyle.Bold};var text=new GUIStyle(GUI.skin.label){fontSize=16};GUI.Label(new Rect(42,34,440,36),planet.displayName.ToUpperInvariant()+" / CONTINUOUS FLIGHT",title);GUI.Label(new Rect(42,77,440,70),$"{(Altitude<0?"Depth":"Altitude")} {(Math.Abs(Altitude)>=1000?(Math.Abs(Altitude)/1000).ToString("N1")+" km":Math.Abs(Altitude).ToString("N1")+" m")}\n{(cruise<0?"DESCENDING":cruise>0?"ASCENDING":"FREE FLIGHT")} · no scene transitions",text);GUI.Label(new Rect(42,h-101,790,78),"1 continuous descent · 2 continuous ascent · WASD steer / cancel cruise\nSpace rise · Ctrl descend · Shift boost · wheel speed · LMB mine / RMB fill � Z undo � F5 save\n"+message,text);if(Cursor.lockState!=CursorLockMode.Locked)GUI.Label(new Rect(w/2-125,h/2+30,320,35),"Click to take control",title);}
 Mesh Sphere(){const int lat=64,lon=128;var v=new Vector3[(lat+1)*(lon+1)];var n=new Vector3[v.Length];var tri=new int[lat*lon*6];int k=0;for(int y=0;y<=lat;y++)for(int x=0;x<=lon;x++){double b=y*Math.PI/lat,a=x*Math.PI*2/lon;int i=y*(lon+1)+x;v[i]=n[i]=new Vector3((float)(Math.Sin(b)*Math.Cos(a)),(float)Math.Cos(b),(float)(Math.Sin(b)*Math.Sin(a)));}for(int y=0;y<lat;y++)for(int x=0;x<lon;x++){int i=y*(lon+1)+x;tri[k++]=i;tri[k++]=i+1;tri[k++]=i+lon+1;tri[k++]=i+1;tri[k++]=i+lon+2;tri[k++]=i+lon+1;}var mesh=new Mesh();mesh.vertices=v;mesh.normals=n;mesh.triangles=tri;mesh.RecalculateBounds();return mesh;}
 void OnDisable(){Cursor.lockState=CursorLockMode.None;Cursor.visible=true;}
 void OnDestroy(){Tiles?.Dispose();if(cloudShell){Destroy(cloudShell.GetComponent<MeshFilter>().sharedMesh);}if(cloud)Destroy(cloud);}
}
}

