﻿Shader "UI/Cozmo/GradientSimpleClippingScreenspaceShader"
{
  Properties
  {
    _Clipping ("DEV ONLY Clipping", Vector) = (0.5, 0.5, 0.5, 0.5)

    _AtlasUV ("DEV ONLY UV", Vector) = (0, 0, 1, 1)
  }
  SubShader
  {
    // No culling or depth
    Cull Off ZWrite Off ZTest Always

    // Alpha blending
    Blend SrcAlpha OneMinusSrcAlpha 

    Pass
    {
      CGPROGRAM
      #pragma vertex vert
      #pragma fragment frag
      
      #include "UnityCG.cginc"

      struct appdata
      {
        float4 vertex : POSITION;
        float2 uv : TEXCOORD0;
      };

      struct v2f
      {
        float2 uv : TEXCOORD0;
        float4 vertex : SV_POSITION;
        float2 topRightAlpha : TEXCOORD1;
        float2 bottomLeftAlpha : TEXCOORD2;
      };

      float4 _AtlasUV;
      float4 _Clipping;

      v2f vert (appdata v)
      {
        v2f o;
        o.vertex = mul(UNITY_MATRIX_MVP, v.vertex);
        o.uv = ((o.vertex + float2(1,1)) * 0.5);
        
        //modify uv x to match screen ratio since we are using PreserveAspect.
        o.uv.x -= 0.5;
        o.uv.x *= 9 * (_ScreenParams.x / _ScreenParams.y) * 0.0625;
        o.uv.x += 0.5;
        o.uv *= _AtlasUV.zw;
        o.uv += _AtlasUV.xy;

        // translate atlas UV to sprite UV
        float2 spriteUV = (v.uv.xy - _AtlasUV.xy) / ( _AtlasUV.zw);

        // precalculate denominator
        float4 denominator = 2 / _Clipping;

        // make the top right of the gradient frame
        float2 topRightAlpha = denominator.xy - spriteUV.xy * denominator.xy;

        // make the bottom left of the gradient frame
        float2 bottomLeftAlpha = spriteUV.xy * denominator.zw;

        o.topRightAlpha = topRightAlpha;
        o.bottomLeftAlpha = bottomLeftAlpha;

        return o;
      }
      
      sampler2D _MainTex;

      fixed4 frag (v2f i) : SV_Target
      {
        fixed4 col = tex2D(_MainTex, i.uv);

        float2 minAlpha2 = min(i.topRightAlpha,i.bottomLeftAlpha);
        col.a = min(col.a, 1 - min(minAlpha2.x, minAlpha2.y));

        return col;
      }
      ENDCG
    }
  }
}