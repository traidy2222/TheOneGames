using System;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using UnityEngine;
using VenusWorld;
namespace Planetary {
// Sparse volumes follow the observer in all three axes; there is no bedrock-depth boundary.
public sealed class FlightExcavation:MonoBehaviour {
 PlanetFlight flight;Material material;LampBounceVolume bounce;float nextEdit,nextSave;bool dirty;
 public PlanetDensityStore Store{get;private set;}
 public PlanetVolumeStream Volume{get;private set;}
 public bool Ready=>Volume!=null&&Volume.Ready;
 public void Bind(PlanetFlight owner){flight=owner;var definition=owner.planet;string identity=FormattableString.Invariant($"volume-v1|{definition.radiusMetres:R}|{definition.elevationDatum:R}|{definition.useMagellan}|{definition.proceduralRelief:R}|{definition.detailAmplitude:R}|{definition.seed}");using var sha=SHA256.Create();string hash=BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(identity))).Replace("-","").Substring(0,16);string path=Path.Combine(Application.persistentDataPath,"PlanetVolumes-v1",string.Concat(Array.ConvertAll(definition.name.ToCharArray(),c=>Array.IndexOf(Path.GetInvalidFileNameChars(),c)>=0?'_':c))+"-"+hash);Store=new PlanetDensityStore(owner.Sampler,path);material=new Material(definition.terrainMaterial);material.SetFloat("_IgnoreDigHole",1);var root=new GameObject("Planet interior stream");root.transform.SetParent(transform,false);Volume=new PlanetVolumeStream(Store,root.transform,material,definition.excavationViewDistance,definition.excavationMeshBudget,owner.SunDirection,definition.ambientDaylight);bounce=new LampBounceVolume();Store.Changed+=(a,b)=>dirty=true;}
 void Update(){Tick();}
 public void Tick(){if(!flight||!flight.Initialized||Store==null)return;Volume.Tick(flight.Position,flight.MetresPerRenderUnit,flight.Altitude<Math.Max(96,flight.planet.excavationViewDistance));if(flight.Altitude<96)bounce.Tick(Volume.LightingView,flight.Position,flight.View.transform.forward,flight.HeadlampOn,flight.HeadlampPosition);if(dirty&&Time.unscaledTime>=nextSave){Save();nextSave=Time.unscaledTime+2;}
  if(!Ready||flight.VerificationInputLock||!Application.isFocused||Cursor.lockState!=CursorLockMode.Locked)return;
  if(Input.GetKeyDown(KeyCode.F5))Save();if(Input.GetKeyDown(KeyCode.F9))Store.Reload();if(Input.GetKeyDown(KeyCode.Z))Store.Undo();
  if(Time.time<nextEdit)return;bool cut=Input.GetMouseButton(0),fill=Input.GetMouseButton(1);if(!cut&&!fill)return;
  var forward=flight.View.transform.forward;var direction=new DVector(forward.x,forward.y,forward.z);
  if(Raycast(flight.Position,direction,12,out var hit,out var normal)){var center=cut?hit-normal*.5:hit+normal*.6;if(fill&&(center-flight.Position).Magnitude<3.5)return;Store.Sculpt(center,2.5,fill);nextEdit=Time.time+.18f;}
 }
 public bool Raycast(DVector origin,DVector direction,double distance,out DVector hit,out DVector normal){direction=direction.Normalized;double previous=0;for(double t=.15;t<=distance;t+=.2){var p=origin+direction*t;if(Store.Sample(p)<0){double lo=previous,hi=t;for(int i=0;i<8;i++){double mid=(lo+hi)*.5;if(Store.Sample(origin+direction*mid)<0)hi=mid;else lo=mid;}hit=origin+direction*((lo+hi)*.5);double e=.2;normal=new DVector(Store.Sample(hit+new DVector(e,0,0))-Store.Sample(hit-new DVector(e,0,0)),Store.Sample(hit+new DVector(0,e,0))-Store.Sample(hit-new DVector(0,e,0)),Store.Sample(hit+new DVector(0,0,e))-Store.Sample(hit-new DVector(0,0,e))).Normalized;return true;}previous=t;}hit=normal=default;return false;}
 public DVector ResolveMovement(DVector from,DVector to){var delta=to-from;double length=delta.Magnitude;if(length<1e-9)return from;var direction=delta*(1/length);double closest=Math.Clamp(-DVector.Dot(from,direction),0,length);double outer=flight.Sampler.Radius+flight.Sampler.ReliefBound+10;if((from+direction*closest).Magnitude>outer)return to;
  double travelled=0;var current=from;
  for(int i=0;i<256&&travelled<length;i++){double baseAir=Store.BaseDensity(current);double clearance=baseAir>64?baseAir:Store.Sample(current);if(clearance<.65)return current;double step=Math.Min(length-travelled,Math.Max(.02,(clearance-.6)*.4));var candidate=current+direction*step;if(Store.Sample(candidate)<.6)return current;if(flight.Sampler.Altitude(candidate)<-1&&!Volume.Contains(candidate,2))return current;current=candidate;travelled+=step;}
  return current;
 }
 public void Save(){Store?.Save();dirty=false;}
 void OnDisable(){Save();Shader.SetGlobalFloat("_VolumeCoverageReady",0);}
 public void DisposeResources(){Volume?.Dispose();bounce?.Dispose();if(material){if(Application.isPlaying)Destroy(material);else DestroyImmediate(material);material=null;}}
 void OnDestroy(){DisposeResources();}
}
}
