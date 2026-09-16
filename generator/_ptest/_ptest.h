#include <QObject>
class TestA : public QObject
{
    int *p, *q;
public:
    TestA(int a, int *b = 0);
    inline int *val() const { return p; }
};
class TestB : public QObject
{
    int p;
public:
    TestB(int a);
    inline int val() const { return p; }
};
class TestC : public QObject
{
public:
    TestC(int a);
    inline int val() const { return p; }
};
