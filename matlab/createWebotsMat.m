function createWebotsMat(images, numCorners, xgrid, ygrid, angles, sizes, names, varargin)


ForegroundColor = [ 14 108 184]/255; % Anki light blue
FiducialColor = zeros(1,3);
%BackgroundColor = [107 107 107]/255; % Anki light gray
BackgroundColor = [1 1 .7]; % Light yellow
ProtoDir = fullfile(fileparts(mfilename('fullpath')), '../robot/simulator/protos');
WorldDir = fullfile(fileparts(mfilename('fullpath')), '../robot/simulator/worlds');

parseVarargin(varargin{:});

if ~iscell(images)
    img = images;
    images = cell(size(xgrid));
    [images{:}] = deal(img);
end

if isscalar(angles)
    angles = angles*ones(size(images));
end

if isscalar(sizes)
    sizes = sizes*ones(size(images));
end

if isscalar(numCorners)
    numCorners = numCorners*ones(size(images));
end

fid = fopen(fullfile(ProtoDir, 'CozmoMat.proto'), 'wt');

if fid == -1
    error('Unable to create proto file "%s".', fullfile(ProtoDir, 'CozomMat.proto'));
end

C = onCleanup(@()fclose(fid));

fprintf(fid, '#VRML_SIM V7.3.0 utf8\n\n');
fprintf(fid, 'PROTO CozmoMat [\n');
fprintf(fid, '  field SFColor Color %.4f %.4f %.4f\n', BackgroundColor(1), BackgroundColor(2), BackgroundColor(3));
fprintf(fid, ']\n');
fprintf(fid, '{\n');
fprintf(fid, 'Solid {\n');
fprintf(fid, '  rotation 0 1 0 0\n');
fprintf(fid, '  translation 0 0 -0.0025\n');
fprintf(fid, '  children [\n');
fprintf(fid, '    Shape {\n');
fprintf(fid, '      appearance Appearance {\n');
fprintf(fid, '         material Material {\n');
fprintf(fid, '		   diffuseColor IS Color\n');
fprintf(fid, '         }\n');
fprintf(fid, '      }\n');
fprintf(fid, '      geometry Box {\n');
fprintf(fid, '        size 1 1 0.005\n');
fprintf(fid, '      }\n');
fprintf(fid, '    }\n');


set(gcf, 'Pos', [100 100 800 650]);

FG = repmat(reshape(ForegroundColor, [1 1 3]), [512 512]);
BG = repmat(reshape(BackgroundColor, [1 1 3]), [512 512]);

codeString = cell(1,numel(images));

for i = 1:numel(images)
        
    %     markerImg = VisionMarker.DrawFiducial( ...
    %         'Image', imrotate(images{i}, -angles(i)), ...
    %         'NumCorners', numCorners(i), ...
    %         'AddPadding', false, ...
    %         'ForegroundColor', ForegroundColor, ...
    %         'BackgroundColor', BackgroundColor);
    markerImg = VisionMarkerTrained.AddFiducial(images{i}, 'PadOutside', false, 'OutputSize', 512, 'FiducialColor', FiducialColor);
    
    if size(markerImg,3)==1
        markerImg = markerImg(:,:,ones(1,3));
    end
    
    markerImg = (1-markerImg).*FG + markerImg.*BG;

    % Need this initial rotation b/c canonical VisionMarker orientation in
    % 3D is vertical, i.e. in the X-Z plane, for historical reasons.
    R_to_flat = rodrigues(-pi/2*[1 0 0]);
    
    pose = Pose(rodrigues(angles(i)*pi/180*[0 0 1])*R_to_flat, ...
        [xgrid(i) ygrid(i) -CozmoVisionProcessor.WHEEL_RADIUS]');
    
    axis = pose.axis;
    angle = pose.angle;
    T = pose.T;
    T(3)= 0;
    [~,nameNoExt,~] = fileparts(names{i});
    codeString{i} = sprintf(['mat->AddMarker(Vision::MARKER_%s,\n', ...
        'Pose3d(%f, {%f,%f,%f}, {%f,%f,%f}),\n%f);\n'], ...
        nameNoExt, angle, ...
        axis(1), axis(2), axis(3), T(1), T(2), T(3), ...
        sizes(i));
        
    filename = sprintf('ankiMat%d.png', i);
    imwrite(imresize(markerImg, [512 512]), fullfile(WorldDir, filename));
    
    fprintf(fid, '    Solid {\n');
    fprintf(fid, '	    translation %.4f %.4f 0.0025\n', xgrid(i)/1000, ygrid(i)/1000);
    fprintf(fid, '	    rotation 0 0 1 %f\n', angles(i)*pi/180);
    fprintf(fid, '	    children [\n');
    fprintf(fid, '		  Shape {\n');
    fprintf(fid, '		    appearance Appearance {\n');
    fprintf(fid, '			  texture ImageTexture { url ["%s"] }\n', filename);
    fprintf(fid, '			  material Material { diffuseColor 1 1 1 }\n');
    fprintf(fid, '		    }\n');
    fprintf(fid, '		    geometry Box {\n');
    fprintf(fid, '            size %f %f 0.00001\n', sizes(i)/1000, sizes(i)/1000 );
    fprintf(fid, '  		}\n');
    fprintf(fid, '		  }\n');
    fprintf(fid, '	    ]\n');
    fprintf(fid, '    }\n');
    
end % FOR each marker

% Add walls
fprintf(fid, 'DEF WALL_1 Solid {\n');
fprintf(fid, '  translation 0.51 0 0.025\n');
fprintf(fid, '  children [\n');
fprintf(fid, '    DEF WALL_SHAPE Shape {\n');
fprintf(fid, '      appearance Appearance {\n');
fprintf(fid, '        material Material {\n');
fprintf(fid, '          diffuseColor 0.12549 0.368627 0.729412\n');
fprintf(fid, '        }\n');
fprintf(fid, '      }\n');
fprintf(fid, '      geometry Box {\n');
fprintf(fid, '        size 0.02 1 0.05\n');
fprintf(fid, '      }\n');
fprintf(fid, '    }\n');
fprintf(fid, '  ]\n');
fprintf(fid, '  boundingObject USE WALL_SHAPE\n');
fprintf(fid, '}\n\n');

fprintf(fid, 'DEF WALL_2 Solid {\n');
fprintf(fid, '  translation -0.51 0 0.025\n');
fprintf(fid, '  children [\n');
fprintf(fid, '    USE WALL_SHAPE\n');
fprintf(fid, '  ]\n');
fprintf(fid, '  boundingObject USE WALL_SHAPE\n');
fprintf(fid, '}\n\n');

fprintf(fid, 'DEF WALL_3 Solid {\n');
fprintf(fid, '  translation 0 -0.51 0.025\n');
fprintf(fid, '  rotation 0 0 1 1.5708\n');
fprintf(fid, '  children [\n');
fprintf(fid, '    USE WALL_SHAPE\n');
fprintf(fid, '  ]\n');
fprintf(fid, '  boundingObject USE WALL_SHAPE\n');
fprintf(fid, '}\n\n');

fprintf(fid, 'DEF WALL_4 Solid {\n');
fprintf(fid, '  translation 0 0.51 0.025\n');
fprintf(fid, '  rotation 0 0 1 1.5708\n');
fprintf(fid, '  children [\n');
fprintf(fid, '    USE WALL_SHAPE\n');
fprintf(fid, '  ]\n');
fprintf(fid, '  boundingObject USE WALL_SHAPE\n');
fprintf(fid, '}\n\n');

fprintf(fid, '  ] # Solid children\n\n');
fprintf(fid, '  contactMaterial "cmat_floor"\n');
fprintf(fid, '  boundingObject Transform {\n');
fprintf(fid, '    rotation 1 0 0 1.5708\n');
fprintf(fid, '    translation 0.5 0.5 0.0025\n');
fprintf(fid, '    children [\n');
fprintf(fid, '      Plane {\n');
fprintf(fid, '      }\n');
fprintf(fid, '    ]\n');
fprintf(fid, '  }\n');
fprintf(fid, '} # Solid\n\n');
fprintf(fid, '} # Proto\n');

% This will copy code to the clipboard that you can paste into BlockWorld 
% constructor to create the markers for the mat with the correct pose
clipboard('copy', [codeString{:}]);
fprintf('\n\nCode copied to clipboard.\n\n');

end % function createWebotsMat()

