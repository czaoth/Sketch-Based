#include <queue>
#include <cstdio>
#include <string>
#include <iostream>
#include "EdgelIndex.h"

const double PI = acos(-1.0);
const double DEG_TRANS = 180.0 / PI;

const int STEP_X[] = { 0, 1, 0, -1, 1, 1, -1, -1 };
const int STEP_Y[] = { 1, 0, -1, 0, 1, -1, 1, -1 };
const int CIRCLE_X[] = { 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8 };
const int CIRCLE_Y[] = { 3, 4, 5, 1, 2, 6, 7, 1, 7, 0, 8, 0, 8, 0, 8, 1, 7, 1, 2, 6, 7, 3, 4, 5 };
const int TOLERANCE_RADIUS = 4;
const int RERANK_NUM = 1000;

EdgelIndex::EdgelIndex() :
    _edgelIndex(200, vector<vector<vector<int>>>(
                200, vector<vector<int>>(
                6, vector<int>())))
{
}


EdgelIndex::~EdgelIndex()
{
}

void EdgelIndex::localDfs(const Sketch &sketch, vector<vector<bool>> &local, const int x, const int y, const int lx, const int ly)
{
    local[lx][ly] = true;
    for (int k = 0; k < 8; ++k)
    {
        int tx = x + STEP_X[k];
        int ty = y + STEP_Y[k];
        if (tx >= 0 && tx < 200)
        {
            if (ty >= 0 && ty < 200)
            {
                if (sketch[tx][ty])
                {
                    int tlx = lx + STEP_X[k];
                    int tly = ly + STEP_Y[k];
                    if (tlx >= 0 && tlx < 9)
                    {
                        if (tly >= 0 && tly < 9)
                        {
                            if (!local[tlx][tly])
                            {
                                localDfs(sketch, local, tx, ty, tlx, tly);
                            }
                        }
                    }
                }
            }
        }

    }
}

/**
* Calculate the angle of the sketch.
* Returns a 200 * 200 * 6 matrix, the last dimension is the angle divided to 6 bins.
*/
vector<vector<vector<bool>>> EdgelIndex::calcAngle(const Sketch &sketch)
{
    vector<vector<vector<bool>>> angle(200, vector<vector<bool>>(200, vector<bool>(6, false)));
    for (int x = 0; x < 200; ++x)
    {
        for (int y = 0; y < 200; ++y)
        {
            if (sketch[x][y])
            {
                vector<vector<bool>> local(9, vector<bool>(9, false));
                localDfs(sketch, local, x, y, 4, 4);
                for (int c = 0; c < 24; ++c)
                {
                    int cx = CIRCLE_X[c];
                    int cy = CIRCLE_Y[c];
                    if (local[cx][cy])
                    {
                        int localAngle = ((int)(atan2(cy - 4.0, cx - 4.0) * DEG_TRANS) + 15 + 360) % 180;
                        angle[x][y][localAngle / 30] = true;
                    }
                }
            }
        }
    }
    return angle;
}

/**
* Generate hit map of the sketch.
* Returns a 200 * 200 * 6 matrix, the last dimension is the angle divided to 6 bins.
*/
vector<vector<vector<bool>>> EdgelIndex::generateHitMap(const Sketch &sketch)
{
    auto hitmap = calcAngle(sketch);
    struct Node
    {
        int x, y, theta;
        int depth;
    };
    queue<Node> q;
    for (int x = 0; x < 200; ++x)
    {
        for (int y = 0; y < 200; ++y)
        {
            for (int theta = 0; theta < 6; ++theta)
            {
                if (hitmap[x][y][theta])
                {
                    q.push({ x, y, theta, 0 });
                }
            }
        }
    }
    while (!q.empty())
    {
        Node node = q.front();
        q.pop();
        if (node.depth < TOLERANCE_RADIUS)
        {
            for (int k = 0; k < 4; ++k)
            {
                int tx = node.x + STEP_X[k];
                int ty = node.y + STEP_Y[k];
                if (tx >= 0 && tx < 200)
                {
                    if (ty >= 0 && ty < 200)
                    {
                        int theta = node.theta;
                        if (!hitmap[tx][ty][theta])
                        {
                            hitmap[tx][ty][theta] = true;
                            q.push({ tx, ty, theta, node.depth + 1 });
                        }
                    }
                }
            }
        }
    }
    return hitmap;
}

void EdgelIndex::generateEdgelIndex(map<int, ImageInfo> &datasetImages, int threadNum)
{
    cout << "Generate Edgel Index: " << endl;
    _shift = 0;
    _threadNum = threadNum;
    _shiftMutex = CreateMutex(NULL, FALSE, NULL);
    _datasetImages = &datasetImages;
    for (int i = 1; i < threadNum; ++i)
    {
        CreateThread(0, 0, edgelThreadEntry, this, 0, 0);
    }
    edgelThread();
    while (_shift)
    {
        Sleep(200);
    }
}

DWORD WINAPI EdgelIndex::edgelThreadEntry(LPVOID self)
{
    reinterpret_cast<EdgelIndex*>(self)->edgelThread();
    return 0;
}

void EdgelIndex::edgelThread()
{
    int len = (int)_datasetImages->size();
    WaitForSingleObject(_shiftMutex, INFINITE);
    int shift = _shift++;
    ReleaseMutex(_shiftMutex);
    vector<vector<vector<vector<int>>>> edgelIndex(200, vector<vector<vector<int>>>(200, vector<vector<int>>(6, vector<int>())));
    for (int i = shift; i < len; i += _threadNum)
    {
        auto image = (*_datasetImages)[i];
        cout << i << ' ' << image.path << endl;
        Sketch sketch(image.path.c_str());
        auto hitmap = generateHitMap(sketch);
        for (int x = 0; x < 200; ++x)
        {
            for (int y = 0; y < 200; ++y)
            {
                for (int theta = 0; theta < 6; ++theta)
                {
                    if (hitmap[x][y][theta])
                    {
                        edgelIndex[x][y][theta].push_back(i);
                    }
                }
            }
        }
    }
    WaitForSingleObject(_shiftMutex, INFINITE);
    for (int x = 0; x < 200; ++x)
    {
        for (int y = 0; y < 200; ++y)
        {
            for (int theta = 0; theta < 6; ++theta)
            {
                for (auto id : edgelIndex[x][y][theta])
                {
                    _edgelIndex[x][y][theta].push_back(id);
                }
            }
        }
    }
    --_shift;
    ReleaseMutex(_shiftMutex);
}

vector<Score> EdgelIndex::query(map<int, ImageInfo> &images, const Sketch &querySketch)
{
    map<int, double> scores;
    auto hitmap = generateHitMap(querySketch);
    for (int x = 0; x < 200; ++x)
    {
        for (int y = 0; y < 200; ++y)
        {
            for (int theta = 0; theta < 6; ++theta)
            {
                if (hitmap[x][y][theta])
                {
                    for (auto id : _edgelIndex[x][y][theta])
                    {
                        scores[id] += 1.0;
                    }
                }
            }
        }
    }
    vector<Score> result;
    for (auto score : scores)
    {
        result.push_back({ score.first, score.second });
    }
    sort(result.begin(), result.end());
    int rerankNum = min((int)result.size(), RERANK_NUM);
    while ((int)result.size() > rerankNum)
    {
        result.pop_back();
    }
    auto angle = calcAngle(querySketch);
    int queryPixelNum = querySketch.countSketchPixel();
    for (int i = (int)result.size() - 1; i >= 0; --i)
    {
        double score = 0.0;
        Sketch sketch(images[result[i].id].path.c_str());
        hitmap = generateHitMap(sketch);
        for (int x = 0; x < 200; ++x)
        {
            for (int y = 0; y < 200; ++y)
            {
                for (int theta = 0; theta < 6; ++theta)
                {
                    if (hitmap[x][y][theta])
                    {
                        if (angle[x][y][theta])
                        {
                            score += 1.0;
                        }
                    }
                }
            }
        }
        result[i].score = (result[i].score / queryPixelNum) * (score / sketch.countSketchPixel());
    }
    sort(result.begin(), result.end());
    // Add missed images. Not necessary.
    for (auto image : images)
    {
        bool find = false;
        for (auto r : result)
        {
            if (r.id == image.first)
            {
                find = true;
                break;
            }
        }
        if (!find)
        {
            result.push_back({ image.first, 0.0 });
        }
    }
    return result;
}

void EdgelIndex::saveHitMap(const vector<vector<vector<bool>>>& hitmap, const char* fileName)
{
    FILE *file = fopen(fileName, "wb");
    for (auto i : hitmap)
    {
        for (auto j : i)
        {
            for (auto k : j)
            {
                fputc(k ? '1' : '0', file);
            }
        }
    }
    fclose(file);
}

vector<vector<vector<bool>>> EdgelIndex::readHitmap(const char* fileName)
{
    vector<vector<vector<bool>>> hitmap(200, vector<vector<bool>>(200, vector<bool>(6, false)));
    FILE *file = fopen(fileName, "rb");
    for (int i = 0; i < 200; ++i)
    {
        for (int j = 0; j < 200; ++j)
        {
            for (int k = 0; k < 6; ++k)
            {
                hitmap[i][j][k] = fgetc(file) != 0;
            }
        }
    }
    fclose(file);
    return hitmap;
}