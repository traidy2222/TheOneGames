using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;
using UnityEngine.Rendering;

namespace Excavation {
public sealed class DigTerrain : MonoBehaviour {
    public int cellsX=144, cellsY=72, cellsZ=144;
    public float spacing=.5f;
    public const int ChunkSize=16;
    public Material material; public bool spherical; public float planetRadius=60;
    public int Seed=173; public System.Func<float,float,float> heightFunction; public System.Func<Vector3,Vector3> metricFunction;
    float[] field;
    readonly Dictionary<Vector3Int, MeshFilter> chunks=new();
    readonly Queue<Vector3Int> dirty=new();
    readonly HashSet<Vector3Int> queued=new();
    readonly List<float[]> undo=new();
    public int PendingChunks=>dirty.Count;
    public int ChunkCount=>chunks.Count;
    public int EditCount {get;private set;}
    public double LastRebuildMilliseconds {get;private set;}
    static readonly Vector3Int[] Corners={new(0,0,0),new(1,0,0),new(1,1,0),new(0,1,0),new(0,0,1),new(1,0,1),new(1,1,1),new(0,1,1)};
    static readonly int[,] Tets={{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}};
    public Vector3 Origin=>new(-cellsX*spacing*.5f,spherical?-cellsY*spacing*.5f:-12,-cellsZ*spacing*.5f);
    int Index(int x,int y,int z)=>(z*(cellsY+1)+y)*(cellsX+1)+x;
    public Vector3 Point(int x,int y,int z)=>Origin+new Vector3(x,y,z)*spacing;
    public float Surface(float x,float z){if(heightFunction!=null)return heightFunction(x,z);
        float broad=Mathf.PerlinNoise((x+Seed)*.018f,(z+Seed)*.018f);
        float detail=Mathf.PerlinNoise((x+51)*.08f,(z+12)*.08f);
        float valley=Mathf.Exp(-Mathf.Pow((x+6*Mathf.Sin(z*.035f))/13,2));
        float distant=Mathf.SmoothStep(0,1,Mathf.InverseLerp(35,160,new Vector2(x,z).magnitude));
        float ridge=1-Mathf.Abs(2*Mathf.PerlinNoise((x+92)*.008f,(z+142)*.008f)-1);
        return 7+broad*7+detail*.9f-valley*2.4f+distant*(12+ridge*45);
    }
    public float RadiusAt(Vector3 direction){
        Vector3 p=direction.normalized*planetRadius;
        float a=(Mathf.PerlinNoise(p.x*.025f+Seed,p.y*.025f+61)+Mathf.PerlinNoise(p.y*.025f+19,p.z*.025f+Seed)+Mathf.PerlinNoise(p.z*.025f+Seed,p.x*.025f+41))/3;
        float b=(Mathf.PerlinNoise(p.x*.09f+13,p.y*.09f+71)+Mathf.PerlinNoise(p.y*.09f+17,p.z*.09f+53)+Mathf.PerlinNoise(p.z*.09f+31,p.x*.09f+29))/3;
        return planetRadius+((a-.5f)*32+Mathf.Max(0,a-.48f)*25*(1-Mathf.Abs(b*2-1))+(b-.5f)*3.5f)*.48f;
    }
    public Vector3 SpawnDirection(){Vector3 best=Vector3.up;float high=-999;for(int i=0;i<160;i++){float y=1-2*(i+.5f)/160;float a=i*2.399963f;Vector3 d=new Vector3(Mathf.Sqrt(1-y*y)*Mathf.Cos(a),y,Mathf.Sqrt(1-y*y)*Mathf.Sin(a));if(d.z>0||d.y<0)continue;float h=RadiusAt(d);if(h>high){high=h;best=d;}}return best;}
    public float DepthAt(Vector3 p)=>spherical?RadiusAt(p)-p.magnitude:Surface(p.x,p.z)-p.y;
    public Vector3 UpAt(Vector3 p)=>spherical?p.normalized:Vector3.up;
    public string MaterialAt(Vector3 p){float depth=DepthAt(p);return depth<.45f?"TOPSOIL":depth<2.4f?"SUBSOIL":"BEDROCK";}
    public float Hardness(Vector3 p)=>MaterialAt(p)=="BEDROCK"?.3f:MaterialAt(p)=="SUBSOIL"?.65f:1;
    public float LastChangedVolume {get;private set;}
    public int Deposit(Vector3 center,float radius){return Sculpt(center,radius,true,true);}

    void Start(){if(field==null) Initialize();}
    public void Initialize(){
        if(field!=null)return;
        if(material==null)material=new Material(Shader.Find("Excavation/Strata"));
        field=new float[(cellsX+1)*(cellsY+1)*(cellsZ+1)];
        for(int z=0;z<=cellsZ;z++)for(int x=0;x<=cellsX;x++){
            float h=Surface(Point(x,0,z).x,Point(x,0,z).z);
            for(int y=0;y<=cellsY;y++) field[Index(x,y,z)]=Boundary(x,y,z)?spacing:(spherical?-DepthAt(Point(x,y,z)):Point(x,y,z).y-h);
        }
        QueueAll(); Flush();
    }
    public System.Collections.IEnumerator InitializeStreamed(){
        if(field!=null)yield break;
        if(material==null)material=new Material(Shader.Find("Excavation/Strata"));
        field=new float[(cellsX+1)*(cellsY+1)*(cellsZ+1)];
        for(int z=0;z<=cellsZ;z++){for(int x=0;x<=cellsX;x++){float h=Surface(Point(x,0,z).x,Point(x,0,z).z);for(int y=0;y<=cellsY;y++)field[Index(x,y,z)]=Boundary(x,y,z)?spacing:Point(x,y,z).y-h;}if(z%4==0)yield return null;}
        QueueAll();while(dirty.Count>0){RebuildNext();yield return null;}Physics.SyncTransforms();
    }
    bool Boundary(int x,int y,int z)=>x==0||y==0||z==0||x==cellsX||y==cellsY||z==cellsZ;
    public float Sample(Vector3 p){
        Vector3 g=(p-Origin)/spacing;
        int x=Mathf.Clamp(Mathf.FloorToInt(g.x),0,cellsX-1),y=Mathf.Clamp(Mathf.FloorToInt(g.y),0,cellsY-1),z=Mathf.Clamp(Mathf.FloorToInt(g.z),0,cellsZ-1);
        float a=Mathf.Clamp01(g.x-x),b=Mathf.Clamp01(g.y-y),c=Mathf.Clamp01(g.z-z);
        return Mathf.Lerp(Mathf.Lerp(Mathf.Lerp(field[Index(x,y,z)],field[Index(x+1,y,z)],a),Mathf.Lerp(field[Index(x,y+1,z)],field[Index(x+1,y+1,z)],a),b),Mathf.Lerp(Mathf.Lerp(field[Index(x,y,z+1)],field[Index(x+1,y,z+1)],a),Mathf.Lerp(field[Index(x,y+1,z+1)],field[Index(x+1,y+1,z+1)],a),b),c);
    }
    Vector3 Gradient(int x,int y,int z)=>new Vector3(field[Index(Mathf.Min(x+1,cellsX),y,z)]-field[Index(Mathf.Max(x-1,0),y,z)],field[Index(x,Mathf.Min(y+1,cellsY),z)]-field[Index(x,Mathf.Max(y-1,0),z)],field[Index(x,y,Mathf.Min(z+1,cellsZ))]-field[Index(x,y,Mathf.Max(z-1,0))]);
    public void BeginStroke(){undo.Add((float[])field.Clone());if(undo.Count>8)undo.RemoveAt(0);}
    public void Smooth(Vector3 center,float radius){
        var copy=(float[])field.Clone(); Vector3 g=(center-Origin)/spacing;int r=Mathf.CeilToInt(radius/spacing);
        for(int z=Mathf.Max(1,(int)g.z-r);z<=Mathf.Min(cellsZ-1,(int)g.z+r);z++)for(int y=Mathf.Max(1,(int)g.y-r);y<=Mathf.Min(cellsY-1,(int)g.y+r);y++)for(int x=Mathf.Max(1,(int)g.x-r);x<=Mathf.Min(cellsX-1,(int)g.x+r);x++){float w=1-Vector3.Distance(Point(x,y,z),center)/radius;if(w<=0)continue;float avg=(copy[Index(x+1,y,z)]+copy[Index(x-1,y,z)]+copy[Index(x,y+1,z)]+copy[Index(x,y-1,z)]+copy[Index(x,y,z+1)]+copy[Index(x,y,z-1)])/6;field[Index(x,y,z)]=Mathf.Lerp(copy[Index(x,y,z)],avg,w*.65f);}
        EditCount++;for(int cz=Mathf.Max(0,((int)g.z-r-2)/ChunkSize);cz<=Mathf.Min((cellsZ-1)/ChunkSize,((int)g.z+r+2)/ChunkSize);cz++)for(int cy=Mathf.Max(0,((int)g.y-r-2)/ChunkSize);cy<=Mathf.Min((cellsY-1)/ChunkSize,((int)g.y+r+2)/ChunkSize);cy++)for(int cx=Mathf.Max(0,((int)g.x-r-2)/ChunkSize);cx<=Mathf.Min((cellsX-1)/ChunkSize,((int)g.x+r+2)/ChunkSize);cx++)Queue(new(cx,cy,cz));
    }
    public bool Undo(){if(undo.Count==0)return false;EditCount++;field=undo[^1];undo.RemoveAt(undo.Count-1);QueueAll();return true;}
    public int Sculpt(Vector3 center,float radius,bool fill,bool mound=false){
        if(!float.IsFinite(center.x)||!float.IsFinite(center.y)||!float.IsFinite(center.z))return 0; if(!float.IsFinite(radius)||radius<spacing*.5f||radius>8)return 0;
        Vector3 g=(center-Origin)/spacing;
        int reach=Mathf.CeilToInt(radius/spacing)+1;
        Vector3Int lo=new(Mathf.Max(1,Mathf.FloorToInt(g.x)-reach),Mathf.Max(1,Mathf.FloorToInt(g.y)-reach),Mathf.Max(1,Mathf.FloorToInt(g.z)-reach));
        Vector3Int hi=new(Mathf.Min(cellsX-1,Mathf.CeilToInt(g.x)+reach),Mathf.Min(cellsY-1,Mathf.CeilToInt(g.y)+reach),Mathf.Min(cellsZ-1,Mathf.CeilToInt(g.z)+reach));
        int changed=0;LastChangedVolume=0;
        for(int z=lo.z;z<=hi.z;z++)for(int y=lo.y;y<=hi.y;y++)for(int x=lo.x;x<=hi.x;x++){
            Vector3 delta=Point(x,y,z)-center;
            Vector3 up=UpAt(center);float vertical=Vector3.Dot(delta,up);float radial=(delta-up*vertical).magnitude;float sphere=mound?Mathf.Max((radial*.7f+vertical-radius*.45f)/1.22f,-vertical-radius*.45f):delta.magnitude-radius;
            // CSG changes only a narrow band around the brush; distant density stays intact.
            if(sphere>spacing*2)continue;
            int i=Index(x,y,z); float v=fill?Mathf.Min(field[i],sphere):Mathf.Max(field[i],-sphere);
            if(Mathf.Abs(v-field[i])<.00001f)continue;
            LastChangedVolume+=Mathf.Abs(Mathf.Clamp01(.5f-v/spacing)-Mathf.Clamp01(.5f-field[i]/spacing))*spacing*spacing*spacing;field[i]=v; changed++;
        }
        if(changed>0){EditCount++;for(int z=Mathf.Max(0,(lo.z-2)/ChunkSize);z<=Mathf.Min((cellsZ-1)/ChunkSize,(hi.z+2)/ChunkSize);z++)for(int y=Mathf.Max(0,(lo.y-2)/ChunkSize);y<=Mathf.Min((cellsY-1)/ChunkSize,(hi.y+2)/ChunkSize);y++)for(int x=Mathf.Max(0,(lo.x-2)/ChunkSize);x<=Mathf.Min((cellsX-1)/ChunkSize,(hi.x+2)/ChunkSize);x++)Queue(new(x,y,z));}
        return changed;
    }
    void Queue(Vector3Int key){if(queued.Add(key))dirty.Enqueue(key);}
    void QueueAll(){for(int z=0;z<cellsZ;z+=ChunkSize)for(int y=0;y<cellsY;y+=ChunkSize)for(int x=0;x<cellsX;x+=ChunkSize)Queue(new(x/ChunkSize,y/ChunkSize,z/ChunkSize));}
    void Update(){var timer=System.Diagnostics.Stopwatch.StartNew();while(dirty.Count>0&&timer.Elapsed.TotalMilliseconds<6)RebuildNext();}
    void RebuildNext(){var k=dirty.Dequeue();queued.Remove(k);var t=System.Diagnostics.Stopwatch.StartNew();Rebuild(k);LastRebuildMilliseconds=t.Elapsed.TotalMilliseconds;}
    public void Flush(){while(dirty.Count>0)RebuildNext();Physics.SyncTransforms();}
    void Rebuild(Vector3Int key){
        if(!chunks.TryGetValue(key,out var filter)){
            var go=new GameObject($"Ground {key.x}:{key.y}:{key.z}");go.transform.SetParent(transform,false);
            filter=go.AddComponent<MeshFilter>();go.AddComponent<MeshRenderer>().sharedMaterial=material;go.AddComponent<MeshCollider>();chunks.Add(key,filter);
        }
        var vertices=new List<Vector3>();var normals=new List<Vector3>();var indices=new List<int>();var colors=new List<Color>();
        Vector3[] p=new Vector3[8],n=new Vector3[8];float[] d=new float[8];int[] inside=new int[4],outside=new int[4];
        Vector3 outward=Vector3.up; Vector3Int start=key*ChunkSize;
        for(int z=start.z;z<Mathf.Min(start.z+ChunkSize,cellsZ);z++)for(int y=start.y;y<Mathf.Min(start.y+ChunkSize,cellsY);y++)for(int x=start.x;x<Mathf.Min(start.x+ChunkSize,cellsX);x++){
            int count=0;
            for(int c=0;c<8;c++){var q=new Vector3Int(x,y,z)+Corners[c];d[c]=field[Index(q.x,q.y,q.z)];if(d[c]<0)count++;p[c]=Point(q.x,q.y,q.z);}
            if(count==0||count==8)continue;
            for(int c=0;c<8;c++){var q=new Vector3Int(x,y,z)+Corners[c];n[c]=Gradient(q.x,q.y,q.z);}
            for(int t=0;t<6;t++){
                int a=0,b=0;for(int j=0;j<4;j++){int c=Tets[t,j];if(d[c]<0)inside[a++]=c;else outside[b++]=c;}
                if(a==0||a==4)continue; Vector3 solid=Vector3.zero,air=Vector3.zero; for(int j=0;j<a;j++)solid+=p[inside[j]];for(int j=0;j<b;j++)air+=p[outside[j]];outward=air/b-solid/a;
                if(a==1)Tri(Edge(inside[0],outside[0]),Edge(inside[0],outside[1]),Edge(inside[0],outside[2]));
                else if(a==3)Tri(Edge(outside[0],inside[0]),Edge(outside[0],inside[1]),Edge(outside[0],inside[2]));
                else {var v0=Edge(inside[0],outside[0]);var v1=Edge(inside[0],outside[1]);var v2=Edge(inside[1],outside[0]);var v3=Edge(inside[1],outside[1]);Tri(v0,v1,v2);Tri(v1,v3,v2);}
            }
        }
        var mesh=filter.sharedMesh;if(mesh==null){mesh=new Mesh {name="Editable ground",indexFormat=IndexFormat.UInt32};mesh.MarkDynamic();}else mesh.Clear();
        foreach(var v in vertices){float depth=DepthAt(v);colors.Add(new Color(Mathf.Clamp01(depth/8),depth>-.12f&&depth<.2f?1:0,0,1));}mesh.SetVertices(vertices);mesh.SetColors(colors);mesh.SetNormals(normals);mesh.SetUVs(1,metricFunction==null?vertices:vertices.ConvertAll(v=>metricFunction(v)));mesh.SetTriangles(indices,0);mesh.RecalculateBounds();filter.sharedMesh=mesh;
        var collider=filter.GetComponent<MeshCollider>();collider.sharedMesh=null;if(indices.Count>0)collider.sharedMesh=mesh;
        (Vector3 p,Vector3 n) Edge(int a,int b){float v=d[a]/(d[a]-d[b]);return(Vector3.Lerp(p[a],p[b],v),Vector3.Lerp(n[a],n[b],v).normalized);}
        void Tri((Vector3 p,Vector3 n) a,(Vector3 p,Vector3 n) b,(Vector3 p,Vector3 n) c){
            Vector3 cross=Vector3.Cross(b.p-a.p,c.p-a.p);if(cross.sqrMagnitude<1e-12f)return;
            if(Vector3.Dot(cross,outward)<0)(b,c)=(c,b);
            int i=vertices.Count;vertices.Add(a.p);vertices.Add(b.p);vertices.Add(c.p);normals.Add(a.n);normals.Add(b.n);normals.Add(c.n);indices.Add(i);indices.Add(i+1);indices.Add(i+2);
        }
    }
    public void Save(string path){
        string tmp=path+".tmp";using(var w=new BinaryWriter(File.Create(tmp))){w.Write(0x45584332);w.Write(cellsX);w.Write(cellsY);w.Write(cellsZ);w.Write(spacing);w.Write(Seed);w.Write(spherical?1:0);w.Write(planetRadius);w.Write(2);foreach(float f in field)w.Write(f);}if(File.Exists(path))File.Replace(tmp,path,null);else File.Move(tmp,path);
    }
    public void Load(string path){
        using var r=new BinaryReader(File.OpenRead(path));
        if(r.ReadInt32()!=0x45584332||r.ReadInt32()!=cellsX||r.ReadInt32()!=cellsY||r.ReadInt32()!=cellsZ||r.ReadSingle()!=spacing||r.ReadInt32()!=Seed||r.ReadInt32()!=(spherical?1:0)||r.ReadSingle()!=planetRadius||r.ReadInt32()!=2||r.BaseStream.Length!=36+field.Length*4L)throw new InvalidDataException("Save does not match this terrain.");
        var next=new float[field.Length];for(int i=0;i<next.Length;i++){next[i]=r.ReadSingle();if(!float.IsFinite(next[i]))throw new InvalidDataException("Invalid density.");}
        for(int z=0;z<=cellsZ;z++)for(int y=0;y<=cellsY;y++)for(int x=0;x<=cellsX;x++)if(Boundary(x,y,z)&&next[Index(x,y,z)]<=0)throw new InvalidDataException("Invalid world boundary.");
        BeginStroke();EditCount++;field=next;QueueAll();
    }
    void OnDestroy(){foreach(var f in chunks.Values)if(f&&f.sharedMesh){if(Application.isPlaying)Destroy(f.sharedMesh);else DestroyImmediate(f.sharedMesh);}}
}
}





