      PROGRAM PSLIB_TEST
        EXTERNAL START_GUI, REGISTER_ACTION
        EXTERNAL ON_LINE, ON_SPLINE
        EXTERNAL ON_SAVE, ON_LOAD
        EXTERNAL ON_NEW, ON_SAVE_AS

        CALL REGISTER_ACTION('LINE', ON_LINE)
        CALL REGISTER_ACTION('SPLINE', ON_SPLINE)

        CALL REGISTER_ACTION('NEW',  ON_NEW)
        CALL REGISTER_ACTION('OPEN', ON_LOAD)
        CALL REGISTER_ACTION('SAVE', ON_SAVE)
        CALL REGISTER_ACTION('SAVE_AS', ON_SAVE_AS)

        CALL START_GUI
      END

      SUBROUTINE ON_LINE
        EXTERNAL PS_GETPOINT, PS_DRAW_LINE
        DOUBLE PRECISION X1, Y1, X2, Y2
        INTEGER HAS_POINT
  
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
        EXTERNAL PS_GETPOINT, PS_DRAW_SPLINE
        INTEGER DEGREE, DIM, TYPE
        INTEGER NCTRL, I, HAS_POINT, STATUS
        DOUBLE PRECISION CTRLPTS(1000), X, Y

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

