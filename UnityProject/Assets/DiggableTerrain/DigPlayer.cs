using System;

using System.IO;

using UnityEngine;

namespace Excavation {

[RequireComponent(typeof(CharacterController))]

public sealed class DigPlayer : MonoBehaviour {

    public DigTerrain terrain;

    public Camera view;

    CharacterController body;PlanetMotor planetMotor;bool Grounded=>terrain.spherical?planetMotor.Grounded:body.isGrounded;

    float yaw,pitch=18,fall,nextEdit;

    public float radius=2.1f;

    bool fly,hasHit; ParticleSystem dust; AudioSource diggingAudio; float excavated;

    RaycastHit hit;

    LineRenderer ring;

    string status="Click to capture mouse. Esc releases it.";

    string SavePath=>Path.Combine(Application.persistentDataPath,terrain.spherical?"planet-ground.bin":"excavation-ground.bin");

    void Start(){

        body=GetComponent<CharacterController>();yaw=transform.eulerAngles.y;

        terrain.Initialize();if(terrain.spherical){planetMotor=gameObject.AddComponent<PlanetMotor>();body.enabled=false;}Respawn();

        dust=new GameObject("Excavation dust").AddComponent<ParticleSystem>();var main=dust.main;main.playOnAwake=false;main.loop=false;main.startLifetime=new ParticleSystem.MinMaxCurve(.45f,.95f);main.startSpeed=new ParticleSystem.MinMaxCurve(.4f,1.2f);main.startSize=new ParticleSystem.MinMaxCurve(.025f,.10f);main.startColor=new Color(.31f,.24f,.15f,.3f);main.gravityModifier=terrain.spherical?0:.7f;main.simulationSpace=ParticleSystemSimulationSpace.World;main.maxParticles=180;var emission=dust.emission;emission.enabled=false;dust.Stop();

        var dustMat=new Material(Shader.Find("Sprites/Default"));dust.GetComponent<ParticleSystemRenderer>().sharedMaterial=dustMat;

        diggingAudio=view.gameObject.AddComponent<AudioSource>();diggingAudio.volume=.15f;var clip=AudioClip.Create("Soil scrape",6000,1,24000,false);var samples=new float[6000];var rand=new System.Random(14);float low=0;for(int i=0;i<samples.Length;i++){low=Mathf.Lerp(low,(float)rand.NextDouble()*2-1,.22f);samples[i]=low*Mathf.Sin(Mathf.PI*i/samples.Length);}clip.SetData(samples,0);diggingAudio.clip=clip;

        ring=new GameObject("Brush footprint").AddComponent<LineRenderer>();ring.loop=true;ring.positionCount=64;ring.startWidth=ring.endWidth=.035f;

        ring.material=new Material(Shader.Find("Sprites/Default"));ring.startColor=ring.endColor=new Color(1,.7f,.15f);

    }

    void Respawn(){body.enabled=false;if(terrain.spherical){Vector3 d=terrain.SpawnDirection();transform.position=d*(terrain.RadiusAt(d)+2);transform.rotation=Quaternion.FromToRotation(Vector3.up,d);}else transform.position=new Vector3(0,terrain.Surface(0,-13)+2,-13);body.enabled=!terrain.spherical;fall=0;}

    void Update(){

        if(Input.GetKeyDown(KeyCode.Escape)){Cursor.lockState=CursorLockMode.None;Cursor.visible=true;}

        if(Cursor.lockState!=CursorLockMode.Locked){if(Input.GetMouseButtonDown(0)){Cursor.lockState=CursorLockMode.Locked;Cursor.visible=false;}return;}

        yaw+=Input.GetAxisRaw("Mouse X")*2;pitch=Mathf.Clamp(pitch-Input.GetAxisRaw("Mouse Y")*2,-85,85);

        if(terrain.spherical){transform.rotation=Quaternion.FromToRotation(transform.up,transform.position.normalized)*transform.rotation;transform.Rotate(Vector3.up,Input.GetAxisRaw("Mouse X")*2,Space.Self);}else transform.rotation=Quaternion.Euler(0,yaw,0);view.transform.localRotation=Quaternion.Euler(pitch,0,0);

        if(Input.GetKeyDown(KeyCode.F)){fly=!fly;fall=0;status=fly?"Survey flight enabled":"Walking enabled";}

        Vector3 move=transform.right*Input.GetAxisRaw("Horizontal")+transform.forward*Input.GetAxisRaw("Vertical");move=Vector3.ClampMagnitude(move,1)*(Input.GetKey(KeyCode.LeftShift)?10:5);

        if(fly)move+=transform.up*((Input.GetKey(KeyCode.Space)?1:0)-(Input.GetKey(KeyCode.LeftControl)?1:0))*7;

        else {if(Grounded&&fall<0)fall=-2;if(Grounded&&Input.GetKeyDown(KeyCode.Space))fall=7;fall-=20*Time.deltaTime;move+=transform.up*fall;}

        if(terrain.spherical)planetMotor.Move(move,Mathf.Min(Time.deltaTime,.05f));else body.Move(move*Time.deltaTime);if((terrain.spherical?transform.position.magnitude<5:transform.position.y < -18) || Input.GetKeyDown(KeyCode.Home))Respawn();

        if(terrain.spherical&&Input.GetKeyDown(KeyCode.O)){body.enabled=false;transform.position=transform.position.normalized*terrain.planetRadius*2.3f;body.enabled=!terrain.spherical;fly=true;pitch=85;status="Orbital survey · Home returns to surface";}
        radius=Mathf.Clamp(radius+Input.mouseScrollDelta.y*.2f,.8f,5);

        if(Input.GetKeyDown(KeyCode.Z)){terrain.Undo();status="Undo requested";}

        if(Input.GetKeyDown(KeyCode.F5)){try{terrain.Save(SavePath);status="Terrain saved";}catch(Exception e){status=e.Message;}}

        if(Input.GetKeyDown(KeyCode.F9)){try{terrain.Load(SavePath);status="Terrain restored";}catch(Exception e){status=e.Message;}}

        hasHit=Physics.Raycast(view.ViewportPointToRay(new Vector3(.5f,.5f,0)),out hit,18);

        if(hasHit&&hit.collider.GetComponentInParent<DigTerrain>()!=terrain)hasHit=false;

        ring.enabled=hasHit;

        if(hasHit){

            Vector3 tangent=Vector3.Cross(hit.normal,Vector3.up);if(tangent.sqrMagnitude<.01f)tangent=Vector3.right;tangent.Normalize();Vector3 bitangent=Vector3.Cross(hit.normal,tangent);

            for(int i=0;i<64;i++){float a=i*Mathf.PI*2/64;ring.SetPosition(i,hit.point+hit.normal*.06f+(tangent*Mathf.Cos(a)+bitangent*Mathf.Sin(a))*radius);}

            bool fill=Input.GetMouseButton(1),dig=Input.GetMouseButton(0);

            if(Input.GetMouseButtonDown(0)||Input.GetMouseButtonDown(1))terrain.BeginStroke();

            if((fill||dig)&&Time.time>=nextEdit&&terrain.PendingChunks==0){

                Vector3 center=fill?hit.point:hit.point+hit.normal*(radius-(.08f+.35f*terrain.Hardness(hit.point)));

                // Prevent depositing solid ground through the player's capsule.

                Vector3 local=transform.InverseTransformPoint(center);Vector3 nearest=transform.TransformPoint(new Vector3(0,Mathf.Clamp(local.y,.35f,1.5f),0));

                if(fill&&Vector3.Distance(center,nearest)<radius+.5f)status="Step back before filling here";

                else {

                    if(Input.GetKey(KeyCode.LeftAlt)&&dig){terrain.Smooth(hit.point,radius);status="Grading surface";}

                    else {if(fill)terrain.Deposit(center,radius);else terrain.Sculpt(center,radius,false);if(!fill)excavated+=terrain.LastChangedVolume;status=fill?"Depositing loose soil":$"Cutting {terrain.MaterialAt(hit.point).ToLowerInvariant()}";dust.transform.position=hit.point;dust.Emit(fill?8:16);diggingAudio.pitch=fill?.8f:1.1f;diggingAudio.Play();}

                }

                nextEdit=Time.time+.12f;

            }

        }

    }

    void OnGUI(){

        float scale=Mathf.Clamp(Screen.width/1500f,.7f,1.3f);GUI.matrix=Matrix4x4.Scale(Vector3.one*scale);float w=Screen.width/scale,h=Screen.height/scale;

        GUI.color=new Color(.055f,.075f,.08f,.94f);GUI.DrawTexture(new Rect(24,24,420,144),Texture2D.whiteTexture);GUI.DrawTexture(new Rect(24,h-130,540,106),Texture2D.whiteTexture);GUI.color=Color.white;

        var title=new GUIStyle(GUI.skin.label){fontSize=25,fontStyle=FontStyle.Bold};var small=new GUIStyle(GUI.skin.label){fontSize=15};

        GUI.Label(new Rect(42,35,380,36),"HIGHLAND  /  EARTHWORKS",title);

        GUI.Label(new Rect(42,78,380,28),terrain.spherical?"SPHERICAL WORLD   ·   RADIAL GRAVITY":"EXCAVATION SITE   ·   72 × 72 m",small);

        GUI.Label(new Rect(42,108,390,48),$"Brush {radius:F1} m   •   {(fly?"FLIGHT":"WALK")}\n{(hasHit?terrain.MaterialAt(hit.point):"SURVEY")}   /   {excavated:F1} m³ excavated (approx.)",small);

        GUI.Label(new Rect(42,h-120,510,90),"WASD move · mouse look · Shift faster · Space jump/up\nLMB dig · RMB fill · wheel brush size · F walk/fly\nAlt+LMB grade · Z undo · F5/F9 save/load · O orbit · Home return",small);

        GUI.Label(new Rect(w-445,h-60,420,38),status,small);

        GUI.color=new Color(1,.8f,.35f);GUI.DrawTexture(new Rect(w/2-7,h/2-1,14,2),Texture2D.whiteTexture);GUI.DrawTexture(new Rect(w/2-1,h/2-7,2,14),Texture2D.whiteTexture);GUI.color=Color.white;

        if(Cursor.lockState!=CursorLockMode.Locked)GUI.Label(new Rect(w/2-180,h/2+30,400,40),"Click to explore and excavate",title);

    }

    void OnDisable(){Cursor.lockState=CursorLockMode.None;Cursor.visible=true;}

    void OnDestroy(){if(dust){Destroy(dust.GetComponent<ParticleSystemRenderer>().sharedMaterial);Destroy(dust.gameObject);}if(diggingAudio&&diggingAudio.clip)Destroy(diggingAudio.clip);if(ring){Destroy(ring.material);Destroy(ring.gameObject);}}

}

}

