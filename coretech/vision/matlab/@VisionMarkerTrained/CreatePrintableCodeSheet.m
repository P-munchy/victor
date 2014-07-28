function CreatePrintableCodeSheet(varargin)
% Create an tiled image of all the trained marker codes, suitable for printing.
%
% CreatePrintableCodeSheet(<ParamName1, value1>, <ParamName2, value2>, ...)
%
%  To print, resize the figure such that the entire page of codes is
%  visible, go to File -> Print Preview, set the following options, and
%  then Print:
%
%   - Set Orientation to Landscape (if specified page size is landscape)
%   - Set Units to Centimeters
%   - Set Placement to Manual, set Left/Top=0, and set Width/Height to
%     match pageSize (see below), converted to cm (8.5"x11" is 
%     width=27.94cm and height=21.59cm)
%
%  Parameters[default]:
%
%  'marginSpacing' [12.5]
%    Border around the edge of the page, in mm.
%
%  'codeSpacing' [10]
%    Distance between markers, in mm.
%
%  'pageWidth' [11*25.4], 'pageHeight' [8.5*25.4]
%    Page size, in mm.
%
%  'markerImageDir' [<empty>]
%    Grab all codes from this directory. Can also be a cell array of 
%    directories, in which case one code sheet per directory is created.
%    If empty (default), one code sheet is created for the directories
%    currently set in VisionMarkerTrained.TrainingImageDir.
%
%  'markerSize' [25]
%    Outside width of the marker fiducial, in mm.
%
%  'numPerCode' [1]
%    Print this many copies of each marker on the sheet (as many as will
%    fit). Use this, for example, to fill up a sheet when there are fewer
%    markers in the directory than will fit on the sheet.
%
%  'outsideBorderWidth' [29]
%    Add a light gray, dotted border of this size around each code. If
%    empty, the border will not be printed.
%
% Example Usage:
%
% Windows: VisionMarkerTrained.CreatePrintableCodeSheet('markerImageDir', {'Z:/Box Sync/Cozmo SE/VisionMarkers/letters/withFiducials/', 'Z:/Box Sync/Cozmo SE/VisionMarkers/symbols/withFiducials', 'Z:/Box Sync/Cozmo SE/VisionMarkers/dice/withFiducials', }, 'outsideBorderWidth', [], 'codeSpacing', 5, 'numPerCode', 2);
%
% ------------
% Andrew Stein
%


nameFilter = 'images';
marginSpacing = 12.5;  % in mm
codeSpacing = 10;     % in mm
pageHeight = 8.5 * 25.4; % in mm
pageWidth  = 11 * 25.4; % in mm
markerImageDir = [];
markerSize = 25; % in mm
numPerCode = 1;
outsideBorderWidth = 29; % in mm, empty to not use

parseVarargin(varargin{:});

assert(isempty(outsideBorderWidth) || outsideBorderWidth > markerSize, ...
    'If specified, outsideBorderWidth should be larger than the markerSize.');

% For now, assume all the training images were in a "rotated" subdir and
% we want to create the test image from the parent of that subdir
upOneDir = '';
if isempty(markerImageDir)
    markerImageDir = VisionMarkerTrained.TrainingImageDir;
    upOneDir = '..';
end

if ~iscell(markerImageDir)
    markerImageDir = { markerImageDir };
end    

numDirs = length(markerImageDir);
fnames = cell(1,numDirs);
for iDir = 1:numDirs
    fnames{iDir} = getfnames(fullfile(markerImageDir{iDir}, upOneDir), nameFilter, 'useFullPath', true);
end
fnames = vertcat(fnames{:});

if isempty(fnames)
    error('No image files found.');
end

numImages = length(fnames);
fprintf('Found %d total images.\n', numImages);
   
numImages = numImages * numPerCode;
fnames = repmat(fnames(:), [numPerCode 1]);

numRows = floor( (pageHeight - 2*marginSpacing + codeSpacing)/(markerSize + codeSpacing) );
numCols = floor( (pageWidth  - 2*marginSpacing + codeSpacing)/(markerSize + codeSpacing) );

xcenters = linspace(marginSpacing+markerSize/2, pageWidth-marginSpacing-markerSize/2, numCols)/10;
ycenters = linspace(marginSpacing+markerSize/2, pageHeight-marginSpacing-markerSize/2,numRows)/10;

[xgrid,ygrid] = meshgrid(xcenters,ycenters);
xgrid = xgrid';
ygrid = ygrid';

%numFigures = ceil(numImages / (numRows*numCols));
% if numRows*numCols < numImages
%     warning('Too many markers for the page. Will leave off %d.', numImages - numRows*numCols);
%     numImages = numRows*numCols;
% end

iFigure = 0;

for iFile = 1:numImages 

    if iFile > iFigure*numRows*numCols
        iFigure = iFigure + 1;
        namedFigure(sprintf('VisionMarkerTrained CodeSheet %d', iFigure), ...
            'Color', 'w', 'Units', 'centimeters');
        
        clf
    
        h_axes = axes('Units', 'centimeters', 'Position', [0 0 pageWidth/10 pageHeight/10]); %#ok<*LAXES>
        set(h_axes, 'XLim', [0 pageWidth/10], 'YLim', [0 pageHeight/10], 'Box', 'on');
        hold(h_axes, 'on')
        axis(h_axes, 'ij');
        
        colormap(h_axes, gray);
        
        if outsideBorderWidth > 0
            borderStr = sprintf('%.1fmm Border, ', outsideBorderWidth);
        else
            borderStr = '';
        end
        text(marginSpacing/10, marginSpacing/20, ...
            sprintf('VisionMarkers, %.1fmm Width, %s%s', ...
            markerSize, borderStr, datestr(now, 31)));
    end
    
    [img, ~, alpha] = imread(fnames{iFile});
    img = mean(im2double(img),3);
    img(alpha < .5) = 1;
    
    iPos = mod(iFile-1, numRows*numCols) + 1;
    imagesc(xgrid(iPos)+markerSize/10*[-.5 .5], ...
        ygrid(iPos)+markerSize/10*[-.5 .5], img);
    
    if outsideBorderWidth > 0
        rectangle('Position', [xgrid(iPos)-outsideBorderWidth/20 ....
            ygrid(iPos)-outsideBorderWidth/20 outsideBorderWidth/10*[1 1]], ...
            'Parent', h_axes, 'EdgeColor', [0.8 0.8 0.8], ...
            'LineWidth', .5, 'LineStyle', ':');
    end
    
end % FOR each File

end % FUNCTION CreatePrintableCodeSheet
