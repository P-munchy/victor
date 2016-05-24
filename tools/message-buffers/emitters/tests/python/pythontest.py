#
#  CLAD Unit test:
#  Python emitter unit test
#

from __future__ import absolute_import
from __future__ import print_function

import math
import sys

import unittest

from SimpleTest import AnkiTypes, Foo, Bar, Baz, Cat, SoManyStrings, ExplicitlyTaggedUnion, AnInt, AFloat, AListOfDoubles, AFixedListOfBytes, ExplicitlyTaggedUnion
from aligned.AutoUnionTest import FunkyMessage, Funky, Monkey, Music
from DefaultValues import IntsWithDefaultValue, FloatsWithDefaultValue

class TestSimpleMessage(unittest.TestCase):

    def test_EmptyMessageConstruction(self):
        foo1a = Foo()
        foo1b = Foo()
        self.assertEqual(foo1a, foo1b, "Empty Message construction is deterministic")

    def test_EmptyMessageRoundTrip(self):
        foo2a = Foo()
        foo2b = Foo.unpack(foo2a.pack())
        self.assertEqual(foo2a, foo2b, "Empty Message round-trip via constructor is broken")
        
    def test_PopulatedMessageRoundTrip(self):
        foo3a = Foo(isFoo=True, myByte=-5, byteTwo=203, myFloat=13231321,
                    myShort=32000, myNormal=0xFFFFFFFF,
                    myFoo=AnkiTypes.AnkiEnum.e3, myString=('laadlsjk' * 10))
        foo3b = Foo.unpack(foo3a.pack())
        self.assertEqual(foo3a, foo3b, "Popluated Message round-trip via constructor is broken")

    def test_PopulatedMessageAssignment(self):
        foo4a = Foo()
        foo4a.isFoo=True
        foo4a.myByte=31
        foo4a.byteTwo=128
        foo4a.myShort=0
        foo4a.myFloat=1323.12
        foo4a.myNormal=65536
        foo4a.myFoo=AnkiTypes.AnkiEnum.e1
        foo4a.myString=''
        
        foo4b = Foo.unpack(foo4a.pack())
        self.assertEqual(foo4a, foo4b, "Populated Message assignment is broken")

class TestMessageOfArrays(unittest.TestCase):

    def test_EmptyMessageConstruction(self):
        bar1a = Bar()
        bar1b = Bar()
        self.assertEqual(bar1a, bar1b, "Empty Message construction is deterministic")
        
    def test_EmptyMessageRoundTrip(self):
        bar2a = Bar()
        bar2b = Bar.unpack(bar2a.pack())
        self.assertEqual(bar2a, bar2b, "Empty Message round-trip via constructor is broken")

    def test_PopulatedMessageRoundTrip(self):
        bar3a = Bar(
            (True, True, True),    #boolBuff
            [-128, 127, 0, 1, -1], #byteBuff
            (-23201, 23201, 102),  #shortBuff
            [AnkiTypes.AnkiEnum.d1], #enumBuff
            [sys.float_info.epsilon, 0, -0, 1, -1, sys.float_info.max, sys.float_info.min,
             sys.float_info.radix, float('inf'), float('-inf')], #doubleBuff
            'long' * 256, #myLongerString
            (i ^ 0x1321 for i in range(20)), #fixedBuff
            (False for i in range(10)), #fixedBoolBuff
            (AnkiTypes.AnkiEnum.d1 for i in range(2)), #fixedEnumBuff
        )
        bar3b = Bar.unpack(bar3a.pack())
        self.assertEqual(bar3a, bar3b, "Populated Message round-trip via constructor is broken")

    def test_PopulatedMessageAssignment(self):
        bar4a = Bar()
        bar4a.boolBuff = (i % 3 == 0 for i in range(200))
        bar4a.byteBuff = ()
        bar4a.shortBuff = tuple(range(255))
        bar4a.enumBuff = [AnkiTypes.AnkiEnum.e3 for i in range(10)]
        bar4a.doubleBuff = ()
        bar4a.myLongerString = ''.join(str(i) for i in range(10))
        bar4a.fixedBuff = [-1] * 20
        bar4a.fixedBoolBuff = (i % 3 == 0 for i in range(10))

        bar4b = Bar.unpack(bar4a.pack())
        self.assertEqual(bar4a, bar4b, "Populated Message assignment is broken")

class TestMessageOfStrings(unittest.TestCase):
    def test_EmptyMessageConstruction(self):
        somany1a = SoManyStrings()
        somany1b = SoManyStrings()
        self.assertEqual(somany1a, somany1b, "Empty Message construction is broken")

    def test_EmptyMessageRoundTrip(self):
        somany2a = SoManyStrings()
        somany2b = SoManyStrings.unpack(somany2a.pack())
        self.assertEqual(somany2a, somany2b, "Empty Message round-trip via constructor is broken")

    def test_PopulatedMessageRoundTrip(self):        
        somany3a = SoManyStrings(
            (hex(x*x*x^3423) for x in range(2000)),
            ('na' * i for i in range(3)),
            ('ads', 'fhg', 'jlk'),
            ('Super', 'Troopers'))
        somany3b = SoManyStrings.unpack(somany3a.pack())
        self.assertEqual(somany3a, somany3b,
                         "Populated Message round-trip via constructor is broken")

    def test_PopulatedMessageAssignment(self):
        somany4a = SoManyStrings()
        somany4a.varStringBuff = (chr(32 + i) for i in range(80))
        somany4a.fixedStringBuff = 'abc'
        somany4a.anotherVarStringBuff = [u'\u1233\u1231 foo', 'asdas', '\xC2\xA2']
        somany4a.anotherFixedStringBuff = ['', '\0']
        
        somany4b = SoManyStrings.unpack(somany4a.pack())
        self.assertEqual(somany4a, somany4b,
                         "Populated Message assignment is broken")

class TestUnion(unittest.TestCase):
    def test_EmptyUnionConstruction(self):
        msg1a = Cat.MyMessage()
        msg1b = Cat.MyMessage()
        self.assertEqual(msg1a, msg1b, "Empty union construction is broken")
        
    def test_EmptyUnionRoundTrip(self):
        msg2a = Cat.MyMessage(myFoo=Foo()) # can't pack untagged
        msg2b = Cat.MyMessage.unpack(msg2a.pack())
        self.assertEqual(msg2a, msg2b, "Empty union roundtrip is broken")
        
    def test_PopulatedUnionRoundTrip(self):
        msg3a = Cat.MyMessage(myFoo=Foo(
            False,
            -128,
            127,
            -23201,
            float('inf'),
            102030,
            0,
            'z' * 255))
        msg3b = Cat.MyMessage.unpack(msg3a.pack())
        self.assertEqual(msg3a, msg3b, "Populated union round-trip is broken")

    def test_PopulatedUnionAssignment(self):
        msg4a = Cat.MyMessage()
        msg4a.myBar = Bar()
        msg4a.myBar.boolBuff = (True, False)
        msg4a.myBar.byteBuff = ()
        msg4a.myBar.shortBuff = (i ^ 0x1321 for i in range(20))
        msg4a.myBar.enumBuff = [AnkiTypes.AnkiEnum.d3 for i in range(10)]
        msg4a.myBar.doubleBuff = [math.sqrt(x**3) for x in range(100)]
        msg4a.myBar.myLongerString = ''.join(str(i) for i in range(100))
        msg4a.myBar.fixedBuff = [sum(range(i)) for i in range(20)]
        msg4a.myBar.fixedBoolBuff = (True,) * 10

        msg4b = Cat.MyMessage.unpack(msg4a.pack())
        self.assertEqual(msg4a, msg4b, "Populated union assignment is broken")

    def test_explicitUnionValues(self):
        testUnion = ExplicitlyTaggedUnion()
        testUnion.anInt = AnInt(42)
        self.assertEqual(0x01, testUnion.tag)
        testUnion.aFloat = AFloat(10.0)
        self.assertEqual(0x02, testUnion.tag)
        testUnion.dList = AListOfDoubles([100.1, 200.2])
        self.assertEqual(0x80, testUnion.tag)
        testUnion.bList = AFixedListOfBytes( (0x00, 0x01, 0x02, 0x03) )
        self.assertEqual(0x81, testUnion.tag)

    def test_introspection(self):
        kind = 'myDog'
        self.assertTrue(hasattr(Cat.MyMessage, kind))
        tag = getattr(Cat.MyMessage.Tag, kind)
        msgType = Cat.MyMessage.typeByTag(tag)
        self.assertIsInstance(msgType(), Baz.Dog)
        subMessage = msgType()
        constructedMessage = Cat.MyMessage(**{kind: subMessage})
        self.assertEqual(constructedMessage.tag, tag)
        self.assertEqual(getattr(constructedMessage, constructedMessage.tag_name), subMessage)
        

class TestAutoUnion(unittest.TestCase):
    def test_Creation(self):
        # this test totally sucks, it needs to assert stuff.
        msg = FunkyMessage()
        funky = Funky(AnkiTypes.AnkiEnum.e1, 3)
        aMonkey = Monkey(1331232132, funky)
        msg.Monkey = aMonkey
        music = Music((131,), funky)
        msg.Music = music
        
class TestDefaultValues(unittest.TestCase):
    def test_defaultValueInts(self):
      # This will break if the default values specified in DefaultValues.clad change
      firstData = IntsWithDefaultValue()
      self.assertEqual(firstData.a, 42)
      self.assertEqual(firstData.b, 0xff)
      self.assertEqual(firstData.c, -2)
      self.assertEqual(firstData.d, True)

      # Ensure we can still fully specify the data
      otherData = IntsWithDefaultValue(1, 1, 1, False)
      self.assertEqual(otherData.a, 1)
      self.assertEqual(otherData.b, 1)
      self.assertEqual(otherData.c, 1)
      self.assertEqual(otherData.d, False)

      # Ensure we can still partially specify the data
      lastData = IntsWithDefaultValue()
      lastData.c = -10
      lastData.d = False
      self.assertEqual(lastData.a, 42)
      self.assertEqual(lastData.b, 0xff)
      self.assertEqual(lastData.c, -10, repr(lastData))
      self.assertEqual(lastData.d, False)

    def test_defaultValuesFloats(self):
      #This will break if the default values specified in DefaultValues.clad change
      firstData = FloatsWithDefaultValue()
      self.assertAlmostEqual(firstData.a, 0.42)
      self.assertAlmostEqual(firstData.b, 12.0)
      self.assertAlmostEqual(firstData.c, 10.0101)
      self.assertAlmostEqual(firstData.d, -2.0)

      # Ensure we can still fully specify the data
      otherData = FloatsWithDefaultValue(1.0, 1.0, 1.0, 1.0)
      self.assertAlmostEqual(otherData.a, 1.0)
      self.assertAlmostEqual(otherData.b, 1.0)
      self.assertAlmostEqual(otherData.c, 1.0)
      self.assertAlmostEqual(otherData.d, 1.0)

      # Ensure we can still partially specify the data
      lastData = FloatsWithDefaultValue()
      lastData.c = -10
      lastData.d = False
      self.assertAlmostEqual(lastData.a, 0.42)
      self.assertAlmostEqual(lastData.b, 12.0)
      self.assertAlmostEqual(lastData.c, -10)
      self.assertAlmostEqual(lastData.d, False)


# Required unittest.main
if __name__ == '__main__':
    unittest.main()
    
