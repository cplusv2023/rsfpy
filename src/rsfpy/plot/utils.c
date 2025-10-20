#include <stdio.h>

int interp_cross(float *X, float *Y, int n1, int ntrace,
                 float *newX, float *newY) {
    int maxlen = 0;

    for (int i2 = 0; i2 < ntrace; i2++) {
        int count = 0;
        float *yy   = Y     + i2 * n1;  
        float *nxrow = newX + i2 * (2*n1); 
        float *nyrow = newY + i2 * (2*n1); 

        if (yy[0] <= 0.0f) {
            nxrow[count] = X[0];
            nyrow[count] = 0.0f;
            count++;
        }

        for (int i1 = 0; i1 < n1 - 1; i1++) {
            if (yy[i1] > 0.0f) {
                nxrow[count] = X[i1];
                nyrow[count] = yy[i1];
                count++;
            }

            if ((yy[i1] > 0 && yy[i1+1] <= 0) || (yy[i1] <= 0 && yy[i1+1] > 0)) {
                float denom = yy[i1] - yy[i1+1];
                if (denom != 0.0f) {
                    float alpha = yy[i1] / denom;
                    float xcross = X[i1] + alpha * (X[i1+1] - X[i1]);
                    nxrow[count] = xcross;
                    nyrow[count] = 0.0f;
                    count++;
                }
            }
        }

        if (yy[n1-1] > 0.0f) {
            nxrow[count] = X[n1-1];
            nyrow[count] = yy[n1-1];
            count++;
        }

        if (yy[n1-1] > 0.0f) {
            nxrow[count] = X[n1-1];
            nyrow[count] = 0.0f;
            count++;
        }

        if (count > maxlen) maxlen = count;
    }

    return maxlen;  
}

