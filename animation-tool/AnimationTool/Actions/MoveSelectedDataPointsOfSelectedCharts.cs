﻿using System.Collections.Generic;
using System.ComponentModel;
using System.Windows.Forms;
using System.Windows.Forms.DataVisualization.Charting;
using System;

namespace AnimationTool
{
    public class MoveSelectedDataPointsOfSelectedCharts : Action
    {
        protected bool left;
        protected bool right;
        protected bool up;
        protected bool down;
        protected double targetXVal;
        protected double targetYVal;
        // we only nudge the target one delta, and don't have a real large x,y target
        protected bool targetNudge;

        protected List<MoveSelectedDataPoints> moveSelectedDataPoints;

        public MoveSelectedDataPointsOfSelectedCharts(List<ChartForm> chartForms, bool left, bool right, bool up, bool down, double targetXVal, double targetYVal)
        {
            this.left = left;
            this.right = right;
            this.up = up;
            this.down = down;
            this.targetXVal = targetXVal;
            this.targetYVal = targetYVal;
            this.targetNudge = false;
            Initialize(chartForms);
        }
        public MoveSelectedDataPointsOfSelectedCharts(List<ChartForm> chartForms, bool left, bool right, bool up, bool down)
        {
            this.left = left;
            this.right = right;
            this.up = up;
            this.down = down;
            this.targetXVal = -1.0;
            this.targetYVal = -1.0;
            this.targetNudge = true;
        }

        private void Initialize(List<ChartForm> chartForms)
        {
            if (chartForms == null) return;

            moveSelectedDataPoints = new List<MoveSelectedDataPoints>();

            foreach (ChartForm chartForm in chartForms)
            {
                if (chartForm.chart.BorderlineColor == SelectChart.borderlineColor)
                {
                    DataPoint dp = chartForm.chart.Series[0].Points[0]; // if chart has sequencer point
                    bool sequencer = dp.IsCustomPropertySet(Sequencer.ExtraData.Key) && Sequencer.ExtraData.Entries.ContainsKey(dp.GetCustomProperty(Sequencer.ExtraData.Key));

                    if (sequencer)
                    {
                        moveSelectedDataPoints.Add(new Sequencer.MoveSelectedDataPoints(chartForm.chart, left, right));
                    }
                    else // else if XYchart
                    {
                        if (this.targetNudge)
                        {
                            moveSelectedDataPoints.Add(new MoveSelectedDataPoints(chartForm.chart, left, right, up, down));
                        }
                        else
                        {
                            moveSelectedDataPoints.Add(new MoveSelectedDataPoints(chartForm.chart, left, right, up, down, targetXVal, targetYVal));
                        }
                    }
                }
            }
        }

        public bool Do()
        {
            if (moveSelectedDataPoints == null || moveSelectedDataPoints.Count == 0) return false;

            for (int i = 0; i < moveSelectedDataPoints.Count; ++i) // find the smallest valid change in x and y
            {
                if (!moveSelectedDataPoints[i].Try())
                {
                    return false;
                }

                if (!moveSelectedDataPoints[i].left)
                {
                    left = false;
                }

                if (!moveSelectedDataPoints[i].right)
                {
                    right = false;
                }

                if (!moveSelectedDataPoints[i].up)
                {
                    up = false;
                }

                if (!moveSelectedDataPoints[i].down)
                {
                    down = false;
                }

                if (!left && !right && !up && !down)
                {
                    return false;
                }
            }

            for (int i = 0; i < moveSelectedDataPoints.Count; ++i)
            {
                moveSelectedDataPoints[i].UpdateValues(left, right, up, down);
            }

            return true;
        }

        public void Undo()
        {
            foreach (MoveSelectedDataPoints moveSelectedDataPoint in moveSelectedDataPoints)
            {
                moveSelectedDataPoint.Undo();
            }
        }
    }
}