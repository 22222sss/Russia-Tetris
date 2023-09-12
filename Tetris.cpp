#include"Tetris.h"

Block blockDefines[7][4]={0};//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

bool IsSetSocketBlocking(int socket, bool blocking) {
    // ��ȡ�׽��ֱ�־
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        close(socket);
        std::cerr << "Failed to get socket flags" << std::endl;
        return false;
    }

    // ���� blocking �����л��������������ģʽ
    if (blocking) {
        flags &= ~O_NONBLOCK;  // �����������־
    }
    else {
        flags |= O_NONBLOCK;  // ���÷�������־
    }

    // �����׽��ֵ��±�־
    if (fcntl(socket, F_SETFL, flags) < 0) {
        close(socket);
        std::cerr << "Failed to set socket mode" << std::endl;
        return false;
    }
    return true;
}

int Color(int c)
{
    switch (c)
    {
    case SHAPE_T:
        return COLOR_PURPLE;//��T���η�������Ϊ��ɫ
    case SHAPE_L:
    case SHAPE_J:
        return COLOR_RED;//��L���κ͡�J���η�������Ϊ��ɫ
    case SHAPE_Z:
    case SHAPE_S:
        return COLOR_LOWBLUE;//��Z���κ͡�S���η�������Ϊǳ��ɫ
    case SHAPE_O:
        return COLOR_YELLO;//��O���η�������Ϊ��ɫ
    case SHAPE_I:
        return COLOR_DEEPBLUE;//��I���η�������Ϊ����ɫ
    default:
        return COLOR_WHITE;
    }
}

void InitBlockInfo()
{
    int i;
    //��T����
    auto& spaceT = blockDefines[SHAPE_T][0].space;
    for (i = 0; i <= 2; i++)
    {
        spaceT[1][i] = 1;
    }
    spaceT[2][1] = 1;

    //��L����
    auto& spaceL = blockDefines[SHAPE_L][0].space;
    for (i = 1; i <= 3; i++)
    {
        spaceL[i][1] = 1;
    }
    spaceL[3][2] = 1;

    //��J����
    auto& spaceJ = blockDefines[SHAPE_J][0].space;
    for (i = 1; i <= 3; i++)
    {
        spaceJ[i][2] = 1;
    }
    spaceJ[3][1] = 1;

    for (int i = 0; i <= 1; i++)
    {
        //��Z����
        auto& spaceZ = blockDefines[SHAPE_Z][0].space;
        spaceZ[1][i] = 1;
        spaceZ[2][i + 1] = 1;
        //��S����
        auto& spaceS = blockDefines[SHAPE_S][0].space;
        spaceS[1][i + 1] = 1;
        spaceS[2][i] = 1;
        //��O����
        auto& spaceO = blockDefines[SHAPE_O][0].space;
        spaceO[1][i + 1] = 1;
        spaceO[2][i + 1] = 1;
    }

    //��I����
    auto& spaceI = blockDefines[SHAPE_I][0].space;
    for (i = 0; i <= 3; i++)
    {
        spaceI[i][1] = 1;
    }

    for (int shape = 0; shape < 7; shape++)//7����״
    {
        for (int form = 0; form < 3; form++)//4����̬���Ѿ�����һ�֣�����ÿ������Ҫ����3�֣�
        {
            int temp[4][4] = { 0 };

            //��ȡ��form��״̬
            for (int i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    temp[i][j] = blockDefines[shape][form].space[i][j];
                }
            }
            //����form����̬˳ʱ����ת���õ���form+1����̬
            for (i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    blockDefines[shape][form + 1].space[i][j] = temp[3 - j][i];
                }
            }
        }
    }
}