// Four-reference EmoteMeshResolver wrapper and bounded graph-search engine.
// Android uses libstdc++ and iOS uses libc++, so their native deque/vector
// headers and object sizes differ. The common source-level containers and
// element types are preserved here; exact ABI layouts live in analysis/.
//
// The four search states remain separate because the two traversal directions
// select different edge endpoints and have distinct close/backtrack rules.

#include "EmoteMeshResolver.h"

#include <cmath>

namespace motion {

    namespace {

        // edgeFind: first edge index i with edge[i].lo <= X && edge[i].hi >= X,
        //   else -1. All references use the inclusive condition `lo <= X &&
        //   hi >= X` and return the first matching interval.
        int edgeFind(const std::vector<std::pair<float, float>>& edge, float X) {
            const int count = static_cast<int>(edge.size());
            for (int i = 0; i < count; ++i) {
                if (edge[i].first <= X && edge[i].second >= X) {
                    return i;
                }
            }
            return -1;
        }

        // Exact float membership in the manually grown visited-value vector.
        bool visitedContains(const std::vector<float>& visited, float val) {
            for (float f : visited) {
                if (f == val) {
                    return true;
                }
            }
            return false;
        }

        // Remove every occurrence of `val`. Both current iOS helpers keep the
        // cursor on the compacted element and continue scanning after erase.
        void visitedEraseAll(std::vector<float>& visited, float val) {
            for (std::size_t k = 0; k < visited.size();) {
                if (visited[k] == val) {
                    visited.erase(visited.begin() + static_cast<long>(k));
                } else {
                    ++k;
                }
            }
        }

        // Return the first row containing `value`. Empty rows are skipped.
        int findNodeRow(
            const std::deque<std::vector<float>>& nodeRows, float value) {
            const int rowCount = static_cast<int>(nodeRows.size());
            for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
                for (float candidate : nodeRows[rowIndex]) {
                    if (candidate == value) {
                        return rowIndex;
                    }
                }
            }
            return -1;
        }

        // The helper assumes a valid row index, as do all four native callers.
        float firstUnvisitedValue(
            const std::deque<std::vector<float>>& nodeRows, int rowIndex,
            const std::vector<float>& visited) {
            for (float candidate : nodeRows[static_cast<std::size_t>(rowIndex)]) {
                if (!visitedContains(visited, candidate)) {
                    return candidate;
                }
            }
            return -1.0f;
        }

        // Scratch containers live for one complete bounded search. The visited
        // vector intentionally survives pass restarts; the two deques do not.
        struct Engine {
            EmoteMeshResolverState* self;
            std::deque<std::pair<float, float>> pathSeg;
            std::deque<float> valueStack;
            std::vector<float> visited;
            float dist = 0.0f;

            void appendSegment(float from, float to) {
                float segmentDist = to - from;
                if (segmentDist < 0.0f) {
                    segmentDist = -segmentDist;
                }
                dist += segmentDist;
                pathSeg.emplace_back(from, to);
            }

            void popSegment() {
                if (pathSeg.empty()) {
                    return;
                }
                const std::pair<float, float>& segment = pathSeg.back();
                float segmentDist = segment.second - segment.first;
                if (segmentDist < 0.0f) {
                    segmentDist = -segmentDist;
                }
                dist -= segmentDist;
                pathSeg.pop_back();
            }

            // Copy the current pair-deque into one owned output row.
            void emitRow(float d) {
                MeshPathRow row;
                row.path = pathSeg;
                row.dist = d;
                self->outputRows.push_back(std::move(row));
            }

            // Exact-float distance deduplication used by the successful close.
            bool rowWithDist(float d) const {
                for (const MeshPathRow& r : self->outputRows) {
                    if (r.dist == d) {
                        return true;
                    }
                }
                return false;
            }
        };

    } // namespace

    // Bounded DFS path search from startValue to endValue through the
    // edge/node value graph.
    static void EmoteMeshResolver_search_guess(EmoteMeshResolverState* self,
                                               float startValue,
                                               float endValue) {
        Engine eng;
        eng.self = self;

        const std::vector<std::pair<float, float>>& edge = self->edgeTable;
        const std::deque<std::vector<float>>& node = self->nodeRows;

        float currentValue = startValue;
        int previousMode = 0;
        int completedPasses = 0;
        bool continuePasses = true;

        while (continuePasses) {
            int state = 0;
            bool passComplete = false;

            while (!passComplete) {
                switch (state & 3) {
                case 0: {
                    eng.pathSeg.clear();
                    eng.dist = 0.0f;
                    eng.valueStack.clear();

                    const int currentEdge = edgeFind(edge, currentValue);
                    if (currentEdge >= 0) {
                        previousMode = 1;
                        state = 1;
                        if (edgeFind(edge, endValue) >= 0) {
                            continue;
                        }
                    }

                    eng.dist = -1.0f;
                    eng.emitRow(eng.dist);
                    continuePasses = false;
                    previousMode = 1;
                    passComplete = true;
                    break;
                }

                case 1: {
                    const int currentEdge = edgeFind(edge, currentValue);
                    const int endEdge = edgeFind(edge, endValue);
                    state = (previousMode == 1) ? 2 : 3;
                    if (currentEdge != endEdge) {
                        continue;
                    }

                    // Native callers enter state 1 only after both values have
                    // resolved. There is deliberately no currentEdge>=0 guard.
                    visitedEraseAll(
                        eng.visited,
                        edge[static_cast<std::size_t>(currentEdge)].first);
                    visitedEraseAll(
                        eng.visited,
                        edge[static_cast<std::size_t>(currentEdge)].second);
                    eng.appendSegment(currentValue, endValue);

                    if (!eng.rowWithDist(eng.dist)) {
                        eng.emitRow(eng.dist);
                    }
                    continuePasses = !eng.valueStack.empty();
                    passComplete = true;
                    break;
                }

                case 2: {
                    const int edgeIndex = edgeFind(edge, currentValue);
                    const float boundary =
                        edge[static_cast<std::size_t>(edgeIndex)].second;
                    const int rowIndex = findNodeRow(node, boundary);
                    if (rowIndex >= 0) {
                        eng.visited.push_back(boundary);
                        const float nextValue =
                            firstUnvisitedValue(node, rowIndex, eng.visited);
                        if (nextValue != -1.0f) {
                            eng.appendSegment(currentValue, boundary);
                            eng.visited.push_back(nextValue);
                            eng.valueStack.push_back(currentValue);
                            currentValue = nextValue;
                            state = 1;
                            continue;
                        }
                    }

                    if (eng.valueStack.empty()) {
                        previousMode = -1;
                        state = 3;
                    } else {
                        currentValue = eng.valueStack.back();
                        eng.popSegment();
                        eng.valueStack.pop_back();
                        state = 2;
                    }
                    continue;
                }

                case 3: {
                    const int edgeIndex = edgeFind(edge, currentValue);
                    const float boundary =
                        edge[static_cast<std::size_t>(edgeIndex)].first;
                    const int rowIndex = findNodeRow(node, boundary);
                    if (rowIndex >= 0) {
                        eng.visited.push_back(boundary);
                        const float nextValue =
                            firstUnvisitedValue(node, rowIndex, eng.visited);
                        if (nextValue != -1.0f) {
                            eng.appendSegment(currentValue, boundary);
                            eng.visited.push_back(nextValue);
                            eng.valueStack.push_back(currentValue);
                            currentValue = nextValue;
                            state = 1;
                            continue;
                        }
                    }

                    if (!eng.valueStack.empty()) {
                        currentValue = eng.valueStack.back();
                        eng.popSegment();
                        eng.valueStack.pop_back();
                        state = 3;
                        continue;
                    }

                    // Final end-side close. A successful row lookup performs
                    // two unvisited scans with the boundary push between them.
                    const int endEdge = edgeFind(edge, endValue);
                    const float endBoundary =
                        edge[static_cast<std::size_t>(endEdge)].second;
                    const int endRow = findNodeRow(node, endBoundary);
                    if (endRow >= 0) {
                        const float closeValue =
                            firstUnvisitedValue(node, endRow, eng.visited);
                        eng.visited.push_back(endBoundary);
                        const float nextValue =
                            firstUnvisitedValue(node, endRow, eng.visited);
                        if (nextValue == -1.0f) {
                            // All four references emit here and then fall
                            // through to the unconditional sentinel below.
                            eng.dist = -1.0f;
                            eng.emitRow(eng.dist);
                        } else {
                            eng.appendSegment(currentValue, closeValue);
                            eng.visited.push_back(nextValue);
                            eng.valueStack.push_back(currentValue);
                            currentValue = nextValue;
                        }
                    }

                    eng.dist = -1.0f;
                    eng.emitRow(eng.dist);
                    return;
                }
                }
            }

            // Post-increment comparison permits ten completed passes: old
            // counter values 0..8 restart, and old value 9 terminates.
            if (completedPasses++ > 8) {
                return;
            }
        }
    }

    void EmoteMeshResolver_resolve_guess(
        EmoteMeshResolverState* self, float startValue, float endValue,
        std::deque<std::pair<float, float>>* valueTrack8B) {
        // Clearing the vector destroys every candidate's owned path deque.
        self->outputRows.clear();

        // The selected path replaces, rather than appends to, the old track.
        valueTrack8B->clear();

        // The native wrappers pass floats in this source-level order on all
        // four targets despite the 32-bit soft-float register presentation.
        EmoteMeshResolver_search_guess(self, startValue, endValue);

        // Pick the first strict minimum whose distance is not the -1 sentinel.
        float bestDist = 99999.0f;
        int best = -1;
        const int rowCount = static_cast<int>(self->outputRows.size());
        for (int i = 0; i < rowCount; ++i) {
            const float d = self->outputRows[i].dist;
            const bool notSentinel = (d != -1.0f);
            const bool less = (d < bestDist);
            if (notSentinel && less) {
                best = i;
                bestDist = d;
            }
        }

        if (best == -1) {
            // No qualifying row, including the >=99999 case.
            valueTrack8B->emplace_back(endValue, endValue);
            self->trackResolvedSpan = 0.0f;
            return;
        }

        self->trackResolvedSpan = bestDist;
        MeshPathRow& row = self->outputRows[best];

        // The wrappers first size the empty destination, then copy from the
        // selected row's back while popping that row. This preserves segment
        // order in the destination and leaves the chosen candidate consumed.
        valueTrack8B->resize(row.path.size());
        for (std::size_t remaining = row.path.size(); remaining > 0;
             --remaining) {
            (*valueTrack8B)[remaining - 1] = row.path.back();
            row.path.pop_back();
        }
    }

} // namespace motion
