function [probeValues, X, Y] = GetProbeValues(img, tform)
% Return values of all probe locations as a ProbeParameters.GridSize^2 image
%
% [probeValues, <X, Y>] = marker.GetProbeValues(img)
%
% 
% ------------
% Andrew Stein
%


if size(img,3) > 1
    img = rgb2gray(img);
end

img = im2double(img);

N = VisionMarkerTrained.ProbeParameters.GridSize;

[xgrid,ygrid] = meshgrid(linspace(VisionMarkerTrained.ProbeRegion(1), ...
    VisionMarkerTrained.ProbeRegion(2), N)); 

pattern = VisionMarkerTrained.ProbePattern;
X = pattern.x(ones(N^2,1),:) + xgrid(:)*ones(1,length(pattern.x));
Y = pattern.y(ones(N^2,1),:) + ygrid(:)*ones(1,length(pattern.y));

%T = maketform('projective', this.H');
[X, Y] = tformfwd(tform, X, Y);

probeValues = mean(interp2(img, X, Y, 'linear', 1), 2);
probeValues = reshape(probeValues, N, N);

end