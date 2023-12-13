#version 430 core

// Raycaster for sprites based on https://lodev.org/cgtutor/raycasting3.html
// this file calculates some values for drawing sprites in the next step

struct spritedata {
    int spriteWidth;
    int spriteHeight;
    float transformY;
    int spriteScreenX;
    ivec2 drawX;
    ivec2 drawY;
    int vMoveScreen;
    uint texture;
};

struct sprite {
    float x;
    float y;
    uint texture;
    int uDiv;
    int vDiv;
    float vMove;
};

layout(local_size_x=1, local_size_y=1) in;
layout(std430, binding=1) buffer dataInput {
    restrict sprite sprites[100];
};
layout(std430, binding=2) buffer dataOutput {
    restrict spritedata spriteResults[100];
};

layout(set = 0, binding = 2) uniform UB0 {
  vec2 position;
  vec2 direction;
  vec2 plane;
  ivec2 screenSize;
} SceneData;

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    uint spriteNum = pixelCoords.x;
    sprite sprite = sprites[spriteNum];

    // translate sprite position to relative to camera
    vec2 spritePos = vec2(sprite.x, sprite.y) - SceneData.position;

    // transform sprite with the inverse camera matrix
    float invDet = 1.0f / (SceneData.plane.x * SceneData.direction.y - SceneData.direction.x * SceneData.plane.y);
    vec2 transform = vec2(
        invDet * (SceneData.direction.y * spritePos.x - SceneData.direction.x * spritePos.y),
        // this is actually the depth inside the screen, that what Z is in 3D
        invDet * (-SceneData.plane.y * spritePos.x + SceneData.plane.x * spritePos.y)
    );

    int spriteScreenX = int((SceneData.screenSize.x * 0.5f) * (1.f + transform.x / transform.y));
    int vMoveScreen = int(sprite.vMove / transform.y);

    // calculate height of the sprite on screen
    //  using 'transformY' instead of the real distance prevents fisheye
    int spriteHeight = abs(int(SceneData.screenSize.y / transform.y)) / sprite.vDiv;
    // calculate lowest and highest pixel to fill in current stripe
    ivec2 drawY = ivec2(
        -spriteHeight * 0.5 + SceneData.screenSize.y * 0.5 + vMoveScreen,
        spriteHeight * 0.5 + SceneData.screenSize.y * 0.5 + vMoveScreen
    );
    if(drawY.x < 0) drawY.x = 0;
    if(drawY.y >= SceneData.screenSize.y) drawY.y = SceneData.screenSize.y - 1;

    // calculate width of the sprite on screen
    int spriteWidth = abs(int(SceneData.screenSize.y / transform.y)) / sprite.uDiv;
    ivec2 drawX = ivec2(
        -spriteWidth * 0.5 + spriteScreenX,
        spriteWidth * 0.5 + spriteScreenX
    );
    if(drawX.x < 0) drawX.x = 0;
    if(drawX.y >= SceneData.screenSize.x) drawX.y = SceneData.screenSize.x - 1;

    spriteResults[spriteNum].spriteWidth = spriteWidth;
    spriteResults[spriteNum].spriteHeight = spriteHeight;
    spriteResults[spriteNum].transformY = transform.y;
    spriteResults[spriteNum].spriteScreenX = spriteScreenX;
    spriteResults[spriteNum].drawX = drawX;
    spriteResults[spriteNum].drawY = drawY;
    spriteResults[spriteNum].vMoveScreen = int(sprite.vMove / transform.y);
    spriteResults[spriteNum].texture = sprite.texture;
}
