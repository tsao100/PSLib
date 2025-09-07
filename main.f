      PROGRAM PSLIB_TEST
        EXTERNAL START_GUI, REGISTER_ACTION
        EXTERNAL ON_LINE, ON_SPLINE, ON_ARC, ON_RECT, ON_POLYLINE
        EXTERNAL ON_SAVE, ON_LOAD
        EXTERNAL ON_NEW, ON_SAVE_AS

        CALL REGISTER_ACTION('LINE', ON_LINE)
        CALL REGISTER_ACTION('ARC',  ON_ARC)
        CALL REGISTER_ACTION('RECT',  ON_RECT)
        CALL REGISTER_ACTION('POLYLINE', ON_POLYLINE)
        CALL REGISTER_ACTION('SPLINE', ON_SPLINE)

        CALL REGISTER_ACTION('NEW',  ON_NEW)
        CALL REGISTER_ACTION('OPEN', ON_LOAD)
        CALL REGISTER_ACTION('SAVE', ON_SAVE)
        CALL REGISTER_ACTION('SAVE_AS', ON_SAVE_AS)

        CALL START_GUI
      END

      SUBROUTINE ON_LINE
        EXTERNAL PS_GETPOINT, PS_DRAW_LINE, PS_SET_ENTITY_MODE
        DOUBLE PRECISION X1, Y1, X2, Y2
        INTEGER HAS_POINT

        CALL PS_SET_ENTITY_MODE('LINE')

        HAS_POINT = 0
        PRINT *, 'Click first point...'
        CALL PS_GETPOINT('Start point: ', X1, Y1, HAS_POINT)
        X2 = X1
        Y2 = Y1

10      IF (HAS_POINT == 1) THEN
          PRINT *, 'Click second point...'
          HAS_POINT = 1
          CALL PS_GETPOINT('Next point: ', X2, Y2, HAS_POINT)
  
          CALL PS_DRAW_LINE(X1, Y1, X2, Y2)
          X1 = X2
          Y1 = Y2  
          GOTO 10
        END IF

  
        PRINT *, 'Line drawn.'

      END

      SUBROUTINE ON_NEW
        CALL PS_NEW_DRAWING
        PRINT *, 'New Untitled drawing created'
      END

      SUBROUTINE ON_SAVE
        CALL PS_SAVE_CURRENT
      END

      SUBROUTINE ON_SAVE_AS
        EXTERNAL POPUP_FILE_DIALOG
        CALL POPUP_FILE_DIALOG(1)
      END

      SUBROUTINE ON_LOAD
        EXTERNAL POPUP_FILE_DIALOG
        CALL POPUP_FILE_DIALOG(0)
      END

      SUBROUTINE ON_SPLINE
        EXTERNAL PS_GETPOINT, PS_DRAW_SPLINE, PS_SET_ENTITY_MODE
        INTEGER DEGREE, DIM, TYPE
        INTEGER NCTRL, I, HAS_POINT, STATUS
        DOUBLE PRECISION CTRLPTS(1000), X, Y

        CALL PS_SET_ENTITY_MODE('SPLINE')

        ! Example: cubic spline, 2D, open
        DEGREE = 3
        DIM = 2
        TYPE = 0   ! 0=open

        PRINT *, 'Click control points (right-click to finish)...'

        NCTRL = 0
        HAS_POINT = 0
10      CONTINUE
        CALL PS_GETPOINT('Click control point...', X, Y, HAS_POINT)
        IF (HAS_POINT == 1) THEN
            NCTRL = NCTRL + 1
            CTRLPTS(2*NCTRL-1) = X
            CTRLPTS(2*NCTRL)   = Y
            GOTO 10
        END IF
        ! Right-click exits loop

        !IF (NCTRL < DEGREE+1) THEN
        !    PRINT *, 'Not enough points to form a spline!'
        !    RETURN
        !END IF

        ! Draw spline using all selected points
        CALL PS_DRAW_SPLINE(CTRLPTS, NCTRL, DEGREE, DIM, TYPE, STATUS)

        PRINT *, 'Spline drawn with', NCTRL, 'control points.'
      END

      SUBROUTINE ON_ARC
        EXTERNAL PS_GETPOINT, PS_DRAW_ARC, PS_SET_ENTITY_MODE
        DOUBLE PRECISION X1, Y1, X2, Y2, X3, Y3
        INTEGER HAS_POINT

        CALL PS_SET_ENTITY_MODE('ARC')

        HAS_POINT = 0
        PRINT *, 'Click arc start point...'
        CALL PS_GETPOINT('Start point', X1, Y1, HAS_POINT)
        X2 = X1
        Y2 = Y1

        PRINT *, 'Click a point on arc...'
        CALL PS_GETPOINT('Point on arc', X2, Y2, HAS_POINT)
        X3 = X2
        Y3 = Y2

        PRINT *, 'Click arc end point...'
        CALL PS_GETPOINT('End point', X3, Y3, HAS_POINT)

        CALL PS_DRAW_ARC(X1, Y1, X2, Y2, X3, Y3)

        PRINT *, 'Arc drawn.'
      END

      SUBROUTINE ON_RECT
        EXTERNAL PS_GETPOINT, PS_DRAW_RECT, PS_SET_ENTITY_MODE
        DOUBLE PRECISION X1, Y1, X2, Y2
        INTEGER HAS_POINT

        CALL PS_SET_ENTITY_MODE('RECT')

        HAS_POINT=0
        PRINT *, 'Click first corner of rectangle...'
        CALL PS_GETPOINT('First corner...', X1, Y1, HAS_POINT)
        X2 = X1
        Y2 = Y1

        PRINT *, 'Click opposite corner...'
        CALL PS_GETPOINT('Opposite corner...', X2, Y2, HAS_POINT)

        CALL PS_DRAW_RECT(X1, Y1, X2, Y2)

        PRINT *, 'Rectangle drawn.'
      END

      SUBROUTINE ON_POLYLINE
        IMPLICIT NONE
        EXTERNAL PS_GETPOINT, PS_DRAW_POLYLINE
        INTEGER MAXPTS
        PARAMETER (MAXPTS = 1000)
        DOUBLE PRECISION X(MAXPTS), Y(MAXPTS)
        INTEGER NPTS, I, HAS_START

        CALL PS_SET_ENTITY_MODE('POLYLINE')

        NPTS = 0
        HAS_START = 0

C --- Get first point ---
        PRINT *, 'Click first point of polyline (ESC to stop)'
        CALL PS_GETPOINT('First point', X(1), Y(1), HAS_START)
        NPTS = 1
        HAS_START = 1

10      CONTINUE
        X(NPTS+1) = X(NPTS)
        Y(NPTS+1) = Y(NPTS)
C --- Get next point ---
        PRINT *, 'Click next point (ENTER=finish)'
        CALL PS_GETPOINT('Next point', X(NPTS+1), Y(NPTS+1), HAS_START)

C User clicked another point
        NPTS = NPTS + 1


        IF (NPTS .GE. MAXPTS) THEN
          PRINT *, 'Too many points!'
          GOTO 99
        END IF
        IF (HAS_START == 1) THEN
            GOTO 10
        END IF

99      CONTINUE
C --- Draw polyline from collected points ---
        IF (NPTS .GT. 1) THEN
           CALL PS_DRAW_POLYLINE(NPTS, X, Y)
           PRINT *, 'Polyline with', NPTS, 'points drawn.'
        ELSE
           PRINT *, 'Polyline canceled (not enough points).'
        END IF

        RETURN
      END



