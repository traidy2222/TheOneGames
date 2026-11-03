using UnityEngine;
namespace Excavation {
// Kinematic capsule sweeps use radial endpoints rather than a world-Y character capsule.
public sealed class PlanetMotor:MonoBehaviour {
 public bool Grounded {get;private set;}
 public Vector3 Up=>transform.position.normalized;
 public void Move(Vector3 velocity,float dt){
  Vector3 position=transform.position,up=Up,remaining=velocity*dt;
  for(int i=0;i<4&&remaining.sqrMagnitude>.0000001f;i++){
   float length=remaining.magnitude;Vector3 dir=remaining/length;
   if(Physics.CapsuleCast(position+up*.36f,position+up*1.44f,.32f,dir,out var hit,length+.025f,~0,QueryTriggerInteraction.Ignore)){
    float travel=Mathf.Max(0,hit.distance-.025f);position+=dir*travel;remaining=Vector3.ProjectOnPlane(remaining-dir*travel,hit.normal);
   }else {position+=remaining;remaining=Vector3.zero;}
  }
  transform.position=position;up=Up;
  Grounded=Physics.SphereCast(position+up*.45f,.28f,-up,out var ground,.27f,~0,QueryTriggerInteraction.Ignore)&&Vector3.Dot(ground.normal,up)>.4f;
 }
}
}
