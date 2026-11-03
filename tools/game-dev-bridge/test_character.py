import copy
import unittest
from character_specs import rig_spec, clip_spec, normalized_weights, validate_spec

class CharacterSpecifications(unittest.TestCase):
    def rig(self):
        return {'meshes':['Body'],'bones':[{'name':'root','head':[0,0,0],'tail':[0,0,1]},
                {'name':'tip','parent':'root','head':[0,0,1],'tail':[0,0,2]}]}

    def test_hierarchy_accepts_unordered_bones(self):
        spec = self.rig(); spec['bones'].reverse(); self.assertEqual(rig_spec(spec),spec)

    def test_hierarchy_rejects_cycles_unknown_parent_multiple_roots(self):
        for change in ({'parent':'tip'},{'parent':'absent'}):
            spec = self.rig(); spec['bones'][0].update(change)
            with self.assertRaises(ValueError): rig_spec(spec)
        spec = self.rig(); del spec['bones'][1]['parent']
        with self.assertRaises(ValueError): rig_spec(spec)

    def test_degenerate_and_nonfinite_bones_fail(self):
        for tail in ([0,0,0],[0,0,float('nan')]):
            spec = self.rig(); spec['bones'][0]['tail'] = tail
            with self.assertRaises(ValueError): rig_spec(spec)

    def test_influences_prune_then_normalize(self):
        weights = normalized_weights({'a':.6,'b':.3,'c':.1}, {'a','b','c'},2)
        self.assertEqual(set(weights), {'a','b'})
        self.assertAlmostEqual(weights['a'], 2/3); self.assertAlmostEqual(sum(weights.values()), 1)

    def test_invalid_weights_do_not_get_fallback_bone(self):
        for weights in ({},{'a':0},{'a':-1},{'a':float('nan')},{'missing':1}):
            with self.assertRaises(ValueError): normalized_weights(weights,{'a'})

    def test_clip_contract(self):
        spec = {'fps':30,'clips':[{'name':'Idle','end':31,'tracks':{'root':[{'frame':1,'location':[0,0,0]},{'frame':31,'location':[1,0,0]}]}}]}
        clip_spec(spec)
        for change in ({'name':'../escape'},{'end':1}):
            bad = copy.deepcopy(spec); bad['clips'][0].update(change)
            with self.assertRaises(ValueError): clip_spec(bad)
        bad = copy.deepcopy(spec); bad['clips'][0]['tracks']['root'][1]['frame']=1
        with self.assertRaises(ValueError): clip_spec(bad)

    def test_export_and_validation_settings_reject_invalid_values(self):
        for op,spec in [('export',{'root_motion':'strip'}),('validate',{'rules':{'sample_frames':1}})]:
            with self.assertRaises(ValueError): validate_spec(op,spec)

if __name__ == '__main__': unittest.main()
