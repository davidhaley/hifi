//
//  Created by Bradley Austin Davis on 2015/08/29
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

var BALL_NAME = "MagBall"
var EDGE_NAME = "MagStick"

var BALL_DIMENSIONS = {
    x: BALL_SIZE,
    y: BALL_SIZE,
    z: BALL_SIZE
};

var BALL_COLOR = {
    red: 128,
    green: 128,
    blue: 128
};

var STICK_DIMENSIONS = {
    x: STICK_LENGTH / 6,
    y: STICK_LENGTH / 6,
    z: STICK_LENGTH
};

var BALL_DISTANCE = STICK_LENGTH + BALL_SIZE;

var BALL_PROTOTYPE = {
    type: "Sphere",
    name: BALL_NAME,
    dimensions: BALL_DIMENSIONS,
    color: BALL_COLOR,
    ignoreCollisions: true,
    collisionsWillMove: false
};

// 2 millimeters
var EPSILON = (.002) / BALL_DISTANCE;

var LINE_DIMENSIONS = {
    x: 5,
    y: 5,
    z: 5
}

var LINE_PROTOTYPE = {
    type: "Line",
    name: EDGE_NAME,
    color: COLORS.CYAN,
    dimensions: LINE_DIMENSIONS,
    lineWidth: 5,
    visible: true,
    ignoreCollisions: true,
    collisionsWillMove: false,
    script: "file:/Users/bdavis/Git/hifi/examples/toys/magSticks/springEdgeEntity.js"
}

var EDGE_PROTOTYPE = LINE_PROTOTYPE;

// var EDGE_PROTOTYPE = {
// type: "Sphere",
// name: EDGE_NAME,
// color: { red: 0, green: 255, blue: 255 },
// //dimensions: STICK_DIMENSIONS,
// dimensions: { x: 0.02, y: 0.02, z: 0.02 },
// rotation: rotation,
// visible: true,
// ignoreCollisions: true,
// collisionsWillMove: false
// }


// A collection of balls and edges connecting them.
MagBalls = function() {
    this.selectedNodes = {};
    Graph.call(this);
    Script.scriptEnding.connect(function() {
        _this.onCleanup();
    });
}

MagBalls.prototype = Object.create( Graph.prototype );

MagBalls.prototype.onUpdate = function(deltaTime) {
    // FIXME move to a physics based implementation as soon as bullet
    // is exposed to entities
}

MagBalls.prototype.createNodeEntity = function(customProperies) {
    return Entities.addEntity(mergeObjects(BALL_PROTOTYPE, customProperies));
}

MagBalls.prototype.createEdgeEntity = function(nodeIdA, nodeIdB) {
    var apos = this.getNodePosition(nodeIdA);
    var bpos = this.getNodePosition(nodeIdB);
    return Entities.addEntity(mergeObjects(EDGE_PROTOTYPE, {
        position: apos,
        linePoints: [ ZERO_VECTOR, Vec3.subtract(bpos, apos) ],
        userData: JSON.stringify({
            magBalls: {
                start: nodeIdA,
                end: nodeIdB,
                length: BALL_DISTANCE
            }
        })
    }));
}

MagBalls.prototype.findPotentialEdges = function(nodeId) {
    var variances = {};
    for (var otherNodeId in this.nodes) {
        // can't self connect 
        if (otherNodeId == nodeId) {
            continue;
        }
        
        // can't doubly connect
        if (this.areConnected(otherNodeId, nodeId)) {
            continue;
        }
        
        // Too far to attempt
        var distance = this.getNodeDistance(nodeId, otherNodeId);
        var variance = this.getVariance(distance);
        if (variance > 0.25) {
            continue;
        }
        
        variances[otherNodeId] = variance;
    }
    return variances;
}

MagBalls.prototype.grabBall = function(position, maxDist) {
    var selected = this.findNearestNode(position, maxDist);
    if (!selected) {
        selected = this.createNode({ position: position });
    } 
    if (selected) {
        this.breakEdges(selected);
        this.selectedNodes[selected] = true;
    }
    return selected;
}

MagBalls.prototype.releaseBall = function(releasedBall) {
    delete this.selectedNodes[releasedBall];
    logDebug("Released ball: " + releasedBall);
    
    // FIXME iterate through the other balls and ensure we don't intersect with
    // any of them. If we do, just delete this ball and return. (play a pop
    // sound)
    
    var releasePosition = this.getNodePosition(releasedBall);

    // Don't overlap other balls
    for (var nodeId in this.nodes) {
        if (nodeId == releasedBall) {
            continue;
        }
        var distance = this.getNodeDistance(releasedBall, nodeId);
        if (distance < BALL_SIZE / 2.0) {
            this.destroyNode(nodeId);
            return;
        }
    }
    
    var targets = this.findPotentialEdges(releasedBall);
    for (var otherBallId in targets) {
        this.createEdge(otherBallId, releasedBall);
    }
    this.clean();
    this.validate();
}


MagBalls.prototype.getVariance = function(distance) {
    // Given two points, how big is the difference between their distance
    // and the desired length length
    return (Math.abs(distance - BALL_DISTANCE)) / BALL_DISTANCE;
}

// remove unconnected balls
MagBalls.prototype.clean = function() {
    // do nothing unless there are at least 2 balls and one edge
    if (Object.keys(this.nodes).length < 2 || !Object.keys(this.edges).length) {
        return;
    }
    var disconnectedNodes = {};
    for (var nodeId in this.nodes) {
        if (!Object.keys(this.nodes[nodeId]).length) {
            disconnectedNodes[nodeId] = true;
        }
    }
    for (var nodeId in disconnectedNodes) {
        this.destroyNode(nodeId);
    }
}

// remove all balls
MagBalls.prototype.clear = function() {
    if (DEBUG_MAGSTICKS) {
        this.deleteAll();
        var ids = Entities.findEntities(MyAvatar.position, 50);
        var result = [];
        ids.forEach(function(id) {
            var properties = Entities.getEntityProperties(id);
            if (properties.name == BALL_NAME || properties.name == EDGE_NAME) {
                Entities.deleteEntity(id);
            }
        }, this);
    }
}
