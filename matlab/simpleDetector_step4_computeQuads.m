function [quads, quadTforms] = simpleDetector_step4_computeQuads(nrows, ncols, numRegions, indexList, centroid, minQuadArea, cornerMethod, computeTransformFromBoundary, quadRefinementMethod, components2d, minDistanceFromImageEdge, embeddedConversions, DEBUG_DISPLAY)

if DEBUG_DISPLAY
    % Show what's left
    namedFigure('SimpleDetector');
    
    h_initialAxes = subplot(221);
    hold off, imshow(img)
    overlay_image(binaryImg, 'r', 0, .3);
    hold on
    
    %centroids = vertcat(stats.WeightedCentroid);
    %plot(centroids(:,1), centroids(:,2), 'b*');
    title(sprintf('%d Initial Detections', numRegions))
end

[xgrid,ygrid] = meshgrid(1:ncols, 1:nrows);

if strcmp(cornerMethod, 'harrisScore')
    Ix = image_right(img) - img; Iy = image_down(img) - img;
    g = gaussian_kernel(.75);
    Ix2 = separable_filter(Ix.^2, g);
    Iy2 = separable_filter(Iy.^2, g);
    IxIy = separable_filter(Ix.*Iy, g);
    cornerScore = Ix2.*Iy2 - IxIy.^2;
end

quads = {};
quadTforms = {};

regionMap = zeros(nrows,ncols);
for i_region = 1:numRegions
    
    regionMap(indexList{i_region}) = i_region;
    
    if strcmp(embeddedConversions.emptyCenterDetection, 'matlab_original')
        % Check to see if interior of this region is roughly empty: 
        x = xgrid(indexList{i_region});
        y = ygrid(indexList{i_region});
        xcen = centroid(i_region,1);
        ycen = centroid(i_region,2);
        x = round(0.5*(x-xcen)+xcen);
        y = round(0.5*(y-ycen)+ycen);
        x = max(x, 1);
        x = min(x, ncols);
        y = max(y, 1);
        y = min(y, nrows);
        interiorIdx = sub2ind([nrows ncols], y, x);
        if any(regionMap(interiorIdx) == i_region)

            if DEBUG_DISPLAY
                namedFigure('InitialFiltering')
                binaryImg(indexList{i_region}) = 0;
                subplot 224
                imshow(binaryImg)
                title('After Interior Check')

                namedFigure('SimpleDetector')
                subplot(h_initialAxes)
                overlay_image(binaryImg, 'r', 0, .3);
            end

           continue; 
        end
    end
    
    % % External boundary
    % [rowStart,colStart] = ind2sub([nrows ncols], ...
    %    stats(i_region).PixelIdxList(1));
    
    if strcmp(embeddedConversions.traceBoundaryType, 'matlab_original') || strcmp(embeddedConversions.traceBoundaryType, 'matlab_loops')
        % Internal boundary
        % Find starting pixel by walking from centroid outward until we hit a
        % pixel in this region:
        rowStart = round(centroid(i_region,2));
        colStart = round(centroid(i_region,1));
        if regionMap(rowStart,colStart) == i_region
            continue;
        end
        while colStart > 1 && regionMap(rowStart,colStart) ~= i_region
            colStart = colStart - 1;
        end

        if colStart == 1 && regionMap(rowStart,colStart) ~= i_region
            continue
        end
    end
    
    if strcmp(embeddedConversions.traceBoundaryType, 'matlab_original')
        try
            assert(~BlockMarker2D.UseOutsideOfSquare, ...
                ['You need to set constant property ' ...
                'BlockMarker2D.UseOutsideOfSquare = false to use this method.']);

            boundary = bwtraceboundary(regionMap == i_region, ...
                [rowStart colStart], 'N');
        catch E
            warning(E.message);
            continue
        end
    elseif strcmp(embeddedConversions.traceBoundaryType, 'matlab_loops')
        binaryImg = uint8(regionMap == i_region);
        boundary = traceBoundary(binaryImg, ...
                [rowStart colStart], 'N');
    elseif strcmp(embeddedConversions.traceBoundaryType, 'c_fixedPoint')
        binaryImg = uint8(regionMap == i_region);
        boundary = mexTraceBoundary(binaryImg, [rowStart, colStart], 'n');
    elseif strcmp(embeddedConversions.traceBoundaryType, 'matlab_approximate')
        if isempty(components2d)
            disp('embeddedConversions.connectedComponentsType must be a run-length encoding version, like matlab_approximate');
            keyboard
        end
        
        boundary = approximateTraceBoundary(components2d{i_region});
%         keyboard
    else
        keyboard
    end
        
    
    if isempty(boundary)
        continue
    end
    
    %plot(boundary(:,2), boundary(:,1), 'b-', 'LineWidth', 2, 'Parent', h_initialAxes);
    %keyboard
    
    switch(cornerMethod)
        case 'harrisScore'
            temp = sub2ind([nrows ncols], boundary(:,1), boundary(:,2));
            r = cornerScore(temp);
        case 'radiusPeaks'
            xcen = mean(boundary(:,2));
            ycen = mean(boundary(:,1));
            [~,r] = cart2pol(boundary(:,2)-xcen, boundary(:,1)-ycen);
            % [~, sortIndex] = sort(theta); % already sorted, thanks to bwtraceboundary
        case 'laplacianPeaks'
            % TODO: vary the smoothing/spacing with boundary length?
            boundaryLength = size(boundary,1);
            sigma = boundaryLength/64;
            spacing = max(3, round(boundaryLength/16)); % spacing about 1/4 of side-length
            stencil = [1 zeros(1, spacing-2) -2 zeros(1, spacing-2) 1];
            g = gaussian_kernel(sigma);
            dg2 = conv(stencil, g);
            r_smooth = imfilter(boundary, dg2(:), 'circular');
            r_smooth = sum(r_smooth.^2, 2);
            
        otherwise
            error('Unrecognzed cornerMethod "%s"', cornerMethod)
    end % SWITCH(cornerMethod)
    
    
    if any(strcmp(cornerMethod, {'harrisScore', 'radiusPeaks'}))
        % Smooth the radial distance from the center according to the
        % total perimeter.
        % TODO: Is there a way to set this magic number in a
        % principaled fashion?
        sigma = size(boundary,1)/64;
        g = gaussian_kernel(sigma); g= g(:);
        r_smooth = imfilter(r, g, 'circular');
    end
    
    % Find local maxima -- these should correspond to the corners
    % of the square.
    % NOTE: one of the comparisons is >= while the other is >, in order to
    % combat rare cases where we have two responses next to each other that
    % are exactly equal.
    localMaxima = find(r_smooth >= r_smooth([end 1:end-1]) & r_smooth > r_smooth([2:end 1]));
        
    if length(localMaxima)>=4
        [~,whichMaxima] = sort(r_smooth(localMaxima), 'descend');
        whichMaxima = sort(whichMaxima(1:4), 'ascend');
        
        % Reorder the indexes to be in the order
        % [corner1, theCornerOppositeCorner1, corner2, corner3]
        index = localMaxima(whichMaxima([1 4 2 3]));
        
        % plot(boundary(index,2), boundary(index,1), 'y+')
        
        corners = fliplr(boundary(index,:));
        
        if DEBUG_DISPLAY
            plot(corners(:,1), corners(:,2), 'gx', 'Parent', h_initialAxes);
        end
        
        % Verify corners are in a clockwise direction, so we don't get an
        % accidental projective mirroring when we do the tranformation below to
        % extract the image.  Can look whether the z direction of cross product
        % of the two vectors forming the quadrilateral is positive or negative.
        A = [corners(2,:) - corners(1,:);
            corners(3,:) - corners(1,:)];
        detA = det(A); % cross product of vectors anchored at corner 1
        if abs(detA) >= minQuadArea
            
            if detA > 0
                corners([2 3],:) = corners([3 2],:);
                %index([2 3]) = index([3 2]);
                detA = -detA;
            end
            
            % One last check: make sure we've got roughly a symmetric
            % quadrilateral (a parallelogram?) by seeing if the area
            % computed using the cross product (via determinates)
            % referenced to opposite corners are similar (and the signs
            % are in agreement, so that two of the sides don't cross
            % each other in the middle or form some sort of weird concave
            % shape).
            B = [corners(3,:) - corners(4,:);
                corners(2,:) - corners(4,:)];
            detB = det(B); % cross product of vectors anchored at corner 4
            
            C = [corners(4,:) - corners(3,:); 
                corners(1,:) - corners(3,:)];
            detC = det(C); % cross product of vectors anchored at corner 3
            
            D = [corners(1,:) - corners(2,:);
                corners(4,:) - corners(2,:)];
            detD = det(D); % cross product of vectors anchored at corner 2
            
            if sign(detA) == sign(detB) && sign(detC) == sign(detD)
                detA = abs(detA);
                detB = abs(detB);
                
                % Is the quad symmetry below the threshold?
                if max(detA,detB) / min(detA,detB) < 1.5
                    
                    tform = []; %#ok<NASGU>
                    if computeTransformFromBoundary
                        % Define each side independently:
                        N = size(boundary,1);
                        
                        Nside = ceil(N/4);
                        
                        tformIsInitialized = false;
                        if strcmp(embeddedConversions.homographyEstimationType, 'matlab_original')
                            try
                                tformInit = cp2tform(corners, [0 0; 0 1; 1 0; 1 1], 'projective');
                                tformIsInitialized = true;
                            catch E
                                warning(['While computing tformInit: ' E.message]);
                                tformInit = [];
                            end
                        elseif strcmp(embeddedConversions.homographyEstimationType, 'opencv_cp2tform')
                            tformInit = mex_cp2tform_projective(corners, [0 0; 0 1; 1 0; 1 1]);
                            tformIsInitialized = true;
                        elseif strcmp(embeddedConversions.homographyEstimationType, 'c_float64')
                            tformInit = mexEstimateHomography(corners, [0 0; 0 1; 1 0; 1 1]);
                            tformIsInitialized = true;
                        end

                        % Test comparing the matlab and openCV versions
%                         homography = mex_cp2tform_projective(corners, [0 0; 0 1; 1 0; 1 1]);
%                         tformIsInitialized = true;
%                         
%                         c1 = tformInit.tdata.T'*[corners,ones(4,1)]';
%                         c1 = c1 ./ repmat(c1(3,:), [3,1]);
%                         
%                         c2 = homography*[corners,ones(4,1)]';
%                         c2 = c2 ./ repmat(c2(3,:), [3,1]);
%                         
%                         keyboard
                        
                        if tformIsInitialized
%                             try
                                canonicalBoundary = ...
                                    [zeros(Nside,1) linspace(0,1,Nside)';  % left side
                                    linspace(0,1,Nside)' ones(Nside,1);  % top
                                    ones(Nside,1) linspace(1,0,Nside)'; % right
                                    linspace(1,0,Nside)' zeros(Nside,1)]; % bottom
                                switch(quadRefinementMethod)
                                    case 'ICP'
                                        if strcmp(embeddedConversions.homographyEstimationType, 'matlab_original')
                                            tform = ICP(fliplr(boundary), canonicalBoundary, ...
                                                'projective', 'tformInit', tformInit, ...
                                                'maxIterations', 10, 'tolerance', .001, ...
                                                'sampleFraction', 1);
                                            
%                                             disp('normal ICP');
%                                             disp(computeHomographyFromTform(tform));
                                        elseif strcmp(embeddedConversions.homographyEstimationType, 'opencv_cp2tform') || strcmp(embeddedConversions.homographyEstimationType, 'c_float64')
                                            tform = ICP_projective(fliplr(boundary), canonicalBoundary, ...
                                                'homographyInit', tformInit, ...
                                                'maxIterations', 10, 'tolerance', .001, ...
                                                'sampleFraction', 1);
%                                             disp('projective ICP');
%                                             disp(tform);
                                            if ~isempty(find(isnan(tform), 1))
                                                tform = tformInit;
                                            end
                                        end                                        
                                    case 'fminsearch'
                                        mag = smoothgradient(img, 1);
                                        %namedFigure('refineHelper')
                                        %hold off, imagesc(mag), axis image off, hold on
                                        %colormap(gray)
                                        options = optimset('TolFun', .1, 'MaxIter', 50);
                                        tform = fminsearch(@(x)refineHelper(x,mag,canonicalBoundary), tformInit.tdata.T, options);
                                        tform = maketform('projective', tform);
                                        
                                    case 'none'
                                        tform = tformInit;
                                        
                                    otherwise
                                        error('Unrecognized quadRefinementMethod "%s"', quadRefinementMethod);
                                end
                                
%                             catch E
%                                 warning(['While refining tform: ' E.message])
%                                 tform = tformInit;
%                             end
                        else
                            tform = [];
                        end
                                                
                        
                        if ~isempty(tform)
                            %if strcmp(quadRefinementMethod, 'ICP') && (strcmp(embeddedConversions.homographyEstimationType, 'opencv_cp2tform') || strcmp(embeddedConversions.homographyEstimationType, 'c_float64'))
                            if (strcmp(embeddedConversions.homographyEstimationType, 'opencv_cp2tform') || strcmp(embeddedConversions.homographyEstimationType, 'c_float64'))
                                
%                                 [x,y] = tforminv(tform, [0 0 1 1]', [0 1 0 1]');
                                tformInv = inv(tform);
                                tformInv = tformInv / tformInv(3,3);
                                xy = tformInv*[0,0,1,1;0,1,0,1;1,1,1,1];
                                xy = xy ./ repmat(xy(3,:), [3,1]);
                                x = xy(1,:)';
                                y = xy(2,:)';
                                
                                % Make the tform normal, so it works with
                                % the other pieces
                                tform = maketform('projective', tform');
                            else % Just a normal Matlab tform
                                % The original corners must have been
                                % visible in the image, and we'd expect
                                % them to all still be within the image
                                % after the transformation adjustment.
                                % Also, the area should still be large
                                % enough
                                % TODO: repeat all the other sanity
                                % checks on the quadrilateral here?
                                [x,y] = tforminv(tform, [0 0 1 1]', [0 1 0 1]');
                                
%                                 disp([x,y])
%                                 
%                                 tformInv = inv(computeHomographyFromTform(tform));
%                                 tformInv = tformInv / tformInv(3,3);
%                                 xy = tformInv*[0,0,1,1;0,1,0,1;1,1,1,1];
%                                 xy = xy ./ repmat(xy(3,:), [3,1]);
%                                 x = xy(1,:)';
%                                 y = xy(2,:)';
                            end
                            
%                             disp([x,y])
%                             
%                             keyboard
                            
                            area = abs((x(2)-x(1))*(y(3)-y(1)) - (x(3)-x(1))*(y(2)-y(1)));
                            if all(round(x) >= (1+minDistanceFromImageEdge) & round(x) <= (ncols-minDistanceFromImageEdge) &...
                                   round(y) >= (1+minDistanceFromImageEdge) & round(y) <= (nrows-minDistanceFromImageEdge)) && ...
                                  area >= minQuadArea

                                corners = [x y];

                                if DEBUG_DISPLAY
                                    plot(corners(:,1), corners(:,2), 'y+', 'Parent', h_initialAxes);
                                end
                            else
                                tform = [];
                            end
                        end
                        
                        
                        %[x,y] = tforminv(tform, ...
                        %    canonicalBoundary(:,1), canonicalBoundary(:,2));
                        %
                        %plot(x, y, 'LineWidth', 2, ...
                        %    'Color', 'm', 'LineWidth', 2);
                        
                        
                    end % IF computeTransformFromBoundary
                    
                    if ~isempty(tform) % tfrom now required by new BlockMarker2D
                        quads{end+1} = corners; %#ok<AGROW>
                        quadTforms{end+1} = tform; %#ok<AGROW>
                    end
                    
                end % IF areas of parallelgrams are similar
                
            end % IF signs of determinants match
            
        end % IF quadrilateral has enough area
        
    end % IF we have at least 4 local maxima
    
end % FOR each region

% keyboard
