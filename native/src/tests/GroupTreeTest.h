#pragma once

namespace rhino
{
// The rules nested track groups are read and reconciled by
// (model/GroupTree.h): throws std::runtime_error naming the first thing that
// is wrong. Run by the geometry runner, which needs no Session and no audio
// device.
void checkGroupTree();
}
