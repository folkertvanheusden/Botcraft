#! /usr/bin/python3

# Written by Folkert van Heusden <folkert@komputilo.nl>


# pip3 install fastmcp
# run as:
# fastmcp run mcp-gateway.py:mcp --transport http --port 8800

import controller
import fastmcp

mcp = fastmcp.FastMCP('Minecraft')

@mcp.tool
def goto(x: int, y: int, z: int) -> None:
    """Move Steve"""
    controller.move_to(x, y, z)


@mcp.tool
def relmove(dx: int, dy: int, dz: int) -> None:
    """Move Steve with relative coordinates"""
    controller.relative_move(dx, dy, dz)


@mcp.tool
def lookat(x: int, y: int, z: int) -> None:
    """Let Steve look at"""
    controller.look_at(x, y, z)


@mcp.tool
def rotate(angle: float) -> None:
    """Rotate Steve"""
    controller.rotate(angle)


@mcp.tool
def state() -> dict:
    """Return client state"""
    return controller.state()


@mcp.tool
def dig(x: int, y: int, z: int) -> None:
    """Let Steve dig a hole"""
    controller.dig(x, y, z)


@mcp.tool
def interact(x: int, y: int, z: int) -> None:
    """Let Steve interact with a block"""
    controller.interact(x, y, z)


@mcp.tool
def screenshot() -> bytes:
    """Return a screenshot in PNG format of what Steve sees"""
    return controller.screenshot()


if __name__ == "__main__":
    mcp.run()
